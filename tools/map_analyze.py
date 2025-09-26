#!/usr/bin/env python3
"""
map_analyze.py

Usage:
  python map_analyze.py path/to/synthe.ino.map [--top N] [--csv out.csv]

What it does:
 - Parse a GNU ld map file and aggregate section sizes (.text/.data/.bss) by object file
 - Prints a sorted table of objects ordered by total contribution (text+data+bss) and shows per-section
 - Optionally writes CSV with columns: object, text, data, bss, total

Limitations:
 - Works with typical ld map produced by arm-none-eabi-gcc/linker. If your map format differs, it may need tweaks.
 - Does not run the linker; it only analyzes the textual map file.

"""
import argparse
import re
import csv
from collections import defaultdict

parser = argparse.ArgumentParser(description='Analyze .map to aggregate sizes by object')
parser.add_argument('mapfile', help='Path to linker .map file')
parser.add_argument('--top', type=int, default=40, help='Show top N entries')
parser.add_argument('--csv', help='Write CSV output')
args = parser.parse_args()

# Regexes to capture lines like:
#  .text          0x00000000        0x70 C:/.../crt0.o
# or lines inside archive listing occasionally
line_re = re.compile(r"^\s*\.(text|data|bss)\s+0x[0-9A-Fa-f]+\s+0x([0-9A-Fa-f]+)\s+(.+)$")

by_obj = defaultdict(lambda: {'text':0,'data':0,'bss':0})

with open(args.mapfile, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        m = line_re.match(line)
        if not m:
            continue
        sec = m.group(1)
        size = int(m.group(2), 16)
        obj = m.group(3).strip()
        # Normalize object names: remove full path prefix to keep name short
        # But keep archive(member) forms intact
        obj_short = obj
        # if path contains colon (Windows absolute), drop the leading path
        # keep the trailing part after last slash/backslash
        if '/' in obj_short or '\\' in obj_short:
            obj_short = re.split(r'[\\/]', obj_short)[-1]
        by_obj[obj_short][sec] += size

# compute totals
rows = []
for obj, d in by_obj.items():
    total = d['text'] + d['data'] + d['bss']
    rows.append((obj, d['text'], d['data'], d['bss'], total))

rows.sort(key=lambda x: x[4], reverse=True)

# Print table
print(f"Top {args.top} objects by total size (text+data+bss):\n")
print(f"{'Total':>8} {'Text':>8} {'Data':>8} {'BSS':>8}  Object")
print('-'*80)
for r in rows[:args.top]:
    print(f"{r[4]:8d} {r[1]:8d} {r[2]:8d} {r[3]:8d}  {r[0]}")

if args.csv:
    with open(args.csv, 'w', newline='', encoding='utf-8') as csvf:
        w = csv.writer(csvf)
        w.writerow(['object','text','data','bss','total'])
        for r in rows:
            w.writerow(r)
    print(f"\nWrote CSV to {args.csv}")

# Extra: can we know at map-time if symbol is unreferenced/stripped?
# The map only shows what the linker considered for placement; if a symbol/object
# does not appear in the map it was not linked in. If it appears under a library
# archive member but size is zero, that probably means not used. There is no
# definitive "will be stripped later" - map is post-link, so it reflects final
# inclusion state. If you see big font tables in the map they are included.

print('\nNotes:')
print(' - If an object does not appear in the map, it was not linked in (not referenced).')
print(" - The map represents the linker's final view: if a symbol/object appears here, it's included in the output (unless the map is partial).")
print(' - Garbage collection of sections (gc-sections) removes unreferenced sections; in that case removed sections won\'t appear in the map.')
