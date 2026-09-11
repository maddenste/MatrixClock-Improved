"""Read-only MatrixClock preset checks; no build, flash, network or file writes.

Requires Python 3.9+, python-dateutil and tzdata. Checks the actual table in the
single-file sketch against one explicit IANA data source, including both sides
of every detected transition. Desktop checks are not ESP8266 execution tests.
"""
# Copyright (C) 2026 Steve Madden
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

from datetime import datetime, timedelta, timezone
from functools import lru_cache
import importlib.resources as resources
import json
from pathlib import Path
import re
from zoneinfo import ZoneInfo

import tzdata
from dateutil.tz import tzstr

ROOT = Path(__file__).resolve().parent.parent
START = datetime(2026, 9, 10, tzinfo=timezone.utc)
END = datetime(2036, 1, 1, tzinfo=timezone.utc)
ZONES = {
    "baker": ["Etc/GMT+12"],
    "american_samoa": ["Pacific/Pago_Pago"],
    "honolulu": ["Pacific/Honolulu"],
    "anchorage": ["America/Anchorage"],
    "los_angeles": ["America/Los_Angeles"],
    "phoenix_vancouver": ["America/Phoenix", "America/Vancouver"],
    "denver": ["America/Denver"],
    "chicago": ["America/Chicago"],
    "mexico_city": ["America/Mexico_City"],
    "new_york": ["America/New_York"],
    "lima": ["America/Lima"],
    "halifax": ["America/Halifax"],
    "santiago": ["America/Santiago"],
    "newfoundland": ["America/St_Johns"],
    "buenos_aires": ["America/Argentina/Buenos_Aires"],
    "south_georgia": ["Atlantic/South_Georgia"],
    "azores": ["Atlantic/Azores"],
    "utc_reykjavik": ["Etc/UTC", "Atlantic/Reykjavik"],
    "london": ["Europe/London"],
    "paris_berlin": ["Europe/Paris", "Europe/Berlin"],
    "athens": ["Europe/Athens"],
    "johannesburg": ["Africa/Johannesburg"],
    "moscow_nairobi": ["Europe/Moscow", "Africa/Nairobi"],
    "tehran": ["Asia/Tehran"],
    "dubai": ["Asia/Dubai"],
    "karachi": ["Asia/Karachi"],
    "almaty": ["Asia/Almaty"],
    "india_sri_lanka": ["Asia/Kolkata", "Asia/Colombo"],
    "dhaka": ["Asia/Dhaka"],
    "yangon": ["Asia/Yangon"],
    "bangkok_jakarta": ["Asia/Bangkok", "Asia/Jakarta"],
    "singapore_beijing": ["Asia/Singapore", "Asia/Shanghai"],
    "tokyo_seoul": ["Asia/Tokyo", "Asia/Seoul"],
    "darwin": ["Australia/Darwin"],
    "sydney_melbourne": ["Australia/Sydney", "Australia/Melbourne"],
    "solomon": ["Pacific/Guadalcanal"],
    "auckland": ["Pacific/Auckland"],
    "fiji": ["Pacific/Fiji"],
    "samoa_tonga": ["Pacific/Apia", "Pacific/Tongatapu"],
    "kiritimati": ["Pacific/Kiritimati"],
}
NAME = r"(?:[A-Za-z]{3,10}|<[A-Za-z0-9+\-]{3,8}>)"
OFFSET = r"[+\-]?(?:[0-9]|1[0-9]|2[0-4])(?::[0-5]?[0-9](?::[0-5]?[0-9])?)?"
TIME = r"(?:[0-9]{1,2}|1[0-5][0-9]|16[0-7])(?::[0-5]?[0-9](?::[0-5]?[0-9])?)?"
TRANSITION = rf"M(?:[1-9]|1[0-2])\.[1-5]\.[0-6](?:/{TIME})?"
RULE = re.compile(
    rf"({NAME})({OFFSET})(?:({NAME})({OFFSET})?,({TRANSITION}),({TRANSITION}))?"
)


def load_source():
    sketches = list(ROOT.glob("MatrixClock_Improved_*.ino"))
    assert len(sketches) == 1, "Keep one current sketch in the release folder"
    source = sketches[0].read_text(encoding="utf-8")
    block = re.search(r"static const Preset PRESETS\[\] PROGMEM = \{(.*?)\n        \};",
                      source, re.S)
    assert block, "Flash-resident preset table not found"
    rows = []
    for line in block[1].splitlines():
        line = line.strip().rstrip(",")
        if line:
            rows.append(json.loads("[" + line[1:-1] + "]"))
    assert [r[0] for r in rows] == list(ZONES)
    assert len(rows) == 40
    for row in rows:
        for value, capacity in zip(row, [32, 56, 16, 40]):
            assert len(value.encode("ascii")) < capacity, (row[0], capacity)
    return source, rows


@lru_cache(None)
def zone(name):
    # Never mix OS zoneinfo with a different pip tzdata database.
    with resources.files("tzdata.zoneinfo").joinpath(*name.split("/")).open("rb") as f:
        return ZoneInfo.from_file(f, key=name)


def duration(text):
    sign = -1 if text.startswith("-") else 1
    parts = [int(p) for p in text.lstrip("+-").split(":")]
    parts += [0] * (3 - len(parts))
    return sign * timedelta(hours=parts[0], minutes=parts[1], seconds=parts[2])


def local_transition(year, text):
    date, _, at = text.partition("/")
    month, week, day = map(int, date[1:].split("."))
    first = datetime(year, month, 1, tzinfo=timezone.utc)
    # Python Monday=0; POSIX Sunday=0.
    first_day = 1 + (day - (first.weekday() + 1) % 7) % 7
    target = first + timedelta(days=first_day - 1 + (week - 1) * 7)
    if target.month != month:
        target -= timedelta(days=7)
    return target + duration(at or "2")


class PosixRule:
    def __init__(self, text):
        match = RULE.fullmatch(text)
        assert match and len(text) < 64, text
        _, standard, dst_name, dst, start, end = match.groups()
        self.standard = -duration(standard)
        self.daylight = -duration(dst) if dst else self.standard + timedelta(hours=1)
        self.start, self.end = start, end
        self.has_dst = dst_name is not None

    @lru_cache(None)
    def boundaries(self, year):
        return (local_transition(year, self.start) - self.standard,
                local_transition(year, self.end) - self.daylight)

    def offset(self, instant):
        if not self.has_dst:
            return self.standard
        start, end = self.boundaries(instant.year)
        summer = start <= instant < end if start < end else instant >= start or instant < end
        return self.daylight if summer else self.standard


def verify_presets(rows):
    boundary_checks = 0
    for ident, label, standard, automatic in rows:
        fixed, active = PosixRule(standard), PosixRule(automatic)
        assert not fixed.has_dst
        # Independent Python library sanity check away from boundaries.
        names = iter(("STD", "DST"))
        compatible = re.sub(r"<[^>]+>", lambda _: next(names), automatic)
        independent = tzstr(compatible, posix_offset=True)
        for year in range(2027, 2036):
            for month in range(1, 13):
                when = datetime(year, month, 15, 12, tzinfo=timezone.utc)
                assert active.offset(when) == when.astimezone(independent).utcoffset(), ident
        for name in ZONES[ident]:
            iana = zone(name)
            def check(when):
                expected = when.astimezone(iana).utcoffset()
                assert active.offset(when) == expected, (ident, name, when, active.offset(when), expected)
            # Test each actual rule boundary one second before, at and after.
            if active.has_dst:
                for year in range(START.year, END.year):
                    for boundary in active.boundaries(year):
                        for delta in (-1, 0, 1):
                            when = boundary + timedelta(seconds=delta)
                            if START <= when < END:
                                check(when)
                                boundary_checks += 1
            # Full timeline sampling, including fixed zones; no footer shortcuts.
            # Detect IANA changes independently and bisect to their exact second.
            when = START
            previous = when
            previous_offset = when.astimezone(iana).utcoffset()
            while when < END:
                check(when)
                current_offset = when.astimezone(iana).utcoffset()
                if current_offset != previous_offset:
                    low, high = previous, when
                    while (high - low).total_seconds() > 1:
                        middle = low + timedelta(seconds=int((high - low).total_seconds()) // 2)
                        if middle.astimezone(iana).utcoffset() == previous_offset:
                            low = middle
                        else:
                            high = middle
                    for delta in (-1, 0, 1):
                        check(high + timedelta(seconds=delta))
                        boundary_checks += 1
                previous, previous_offset = when, current_offset
                when += timedelta(hours=6)
            # Compare fixed-standard mode to the base offset after any 2026
            # legal reclassifications (e.g. Vancouver permanent UTC-7).
            for year in range(2027, 2036):
                for month in range(1, 13):
                    local = datetime(year, month, 15, 12, tzinfo=timezone.utc).astimezone(iana)
                    assert fixed.standard == local.utcoffset() - local.dst(), (ident, name, local)
        print(f"PASS {ident}")
    return boundary_checks


def verify_source_contracts(source, rows):
    # Structural checks only; this deliberately does not claim to execute C++.
    by_id = {r[0]: r for r in rows}
    aliases = re.findall(r'(?:if|else if) \(strcmp\(id, "([^"]+)"\) == 0\) id = "([^"]+)";', source)
    expected = {"Europe/London", "Europe/Paris", "America/New_York",
                "America/Los_Angeles", "Australia/Sydney", "UTC0"}
    assert {key for key, _ in aliases} == expected
    for old, new in aliases:
        assert new in by_id, old
    legacy = ["UTC+12", "UTC+11", "UTC+10", "UTC+9", "UTC+8", "UTC+7",
              "UTC+6", "UTC+5", "UTC+4", "UTC+3:30", "UTC+3", "UTC+2",
              "UTC+1", "UTC0", "UTC-2", "UTC-3", "UTC-3:30", "UTC-4",
              "UTC-5", "UTC-5:30", "UTC-6", "UTC-6:30", "UTC-7",
              "UTC-8", "UTC-9", "UTC-9:30", "UTC-11", "UTC-12", "UTC-13", "UTC-14"]
    for text in legacy:
        parsed = PosixRule(text)
        assert not parsed.has_dst
        assert parsed.standard == -duration(text[3:])
    assert 'else selected = id;' in source
    assert 'else if (!fixedUtc(id))' in source
    assert 'static_cast<uint8_t>(dstMode.toInt())' in source
    save = source[source.index("void handleWebSave() {"):source.index("void eraseAllClockState()")]
    assert save.index("validSelection(") < save.index("previousSettings = settings")
    assert save.index("validSelection(") < save.index("commitDriftCalibrationIfReady()")
    assert 'if (web.hasArg("dst")) settings.daylightSaving' not in save
    assert "timezoneId.length() != strlen(timezoneId.c_str())" in save
    assert "customRule.length() != strlen(customRule.c_str())" in save
    assert "rule.disabled=!custom;rule.required=custom" in source
    assert "maxlength='63'" in source
    assert "tzValues[]" not in source
    print(f"PASS source integration contracts; {len(aliases)} aliases and {len(legacy)} legacy fixed rules")


if __name__ == "__main__":
    source, rows = load_source()
    verify_source_contracts(source, rows)
    checks = verify_presets(rows)
    print(f"VERIFIED {len(rows)}/40 presets; {checks} transition-boundary checks; "
          f"IANA tzdata {tzdata.__version__}; {START.date()} through 2035.")
    print("Desktop data/model and source-structure checks only; no firmware compile or hardware test.")
