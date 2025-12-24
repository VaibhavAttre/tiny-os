#!/usr/bin/env python3
import sys
import re

tick = None
in_csv = False
header = None
printed_header = False

tick_re = re.compile(r'^(SNAPSHOT|FINAL),tick=(\d+)\s*$')

for line in sys.stdin:
    line = line.strip()

    m = tick_re.match(line)
    if m:
        tick = int(m.group(2))
        continue

    if line == "CSV":
        in_csv = True
        header = None
        continue

    if line == "CSV_END":
        in_csv = False
        continue

    if not in_csv:
        continue

    # first line after CSV is header
    if header is None:
        header = line
        if not printed_header:
            print("tick," + header)
            printed_header = True
        continue

    # data line: "id,run_ticks,..."
    if tick is None:
        # if no SNAPSHOT seen yet, skip
        continue

    print(f"{tick},{line}")
