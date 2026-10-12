#!/usr/bin/env python3
# Rebuilds data/carbon-intensity.csv and its built-in copy, src/carbon_data.c, from Ember's
# yearly electricity data (CC BY 4.0, https://ember-energy.org/data/yearly-electricity-data/):
# the latest carbon intensity of each country, and of the world.
#
# Ember updates its data twice a month, new yearly figures come once a year. Run from anywhere:
#   python3 data/update.py              downloads Ember's file
#   python3 data/update.py FILE.csv     uses a file already downloaded

import csv
import io
import pathlib
import sys
import unicodedata
import urllib.request

URL = "https://files.ember-energy.org/public-downloads/generation/outputs/release_generation_yearly_global.csv"
ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = "ISO 3 code,Area,Year,Emissions intensity (gCO2e/kWh)"

if len(sys.argv) > 1:
    source = open(sys.argv[1], encoding="utf-8", newline="")
else:
    source = io.TextIOWrapper(urllib.request.urlopen(URL), encoding="utf-8", newline="")

latest = {}
for row in csv.DictReader(source):
    grams = row["Emissions intensity (gCO2e/kWh)"]
    if row["Electricity source"] != "Total generation" or not grams:
        continue
    if row["Area type"] != "Country or economy" and row["Area"] != "World":
        continue
    # A few rows are negative, from bad data: the year before is used
    if float(grams) <= 0:
        continue
    key = (row["ISO 3 code"], row["Area"])
    if key not in latest or int(row["Year"]) > latest[key][0]:
        latest[key] = (int(row["Year"]), float(grams))

# World first, then by name, Türkiye among the T's
def order(item):
    name = item[0][1]
    return name != "World", unicodedata.normalize("NFKD", name).encode("ascii", "ignore")

lines = [HEADER]
for (code, name), (year, grams) in sorted(latest.items(), key=order):
    name.encode("latin-1")   # fails on a letter the program's font doesn't have
    lines.append(f"{code},{name},{year},{grams:.1f}")

(ROOT / "data" / "carbon-intensity.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")

# Octal escapes keep the C file ASCII
def c_string(line):
    return "".join(chr(b) if 32 <= b < 127 and b not in b'"\\' else f"\\{b:03o}" for b in line.encode("utf-8"))

with open(ROOT / "src" / "carbon_data.c", "w", encoding="ascii") as out:
    out.write("/* Made by data/update.py from Ember's yearly electricity data (CC BY 4.0), the same as\n"
              " * data/carbon-intensity.csv: used when that file isn't installed */\n\n"
              "const char carbon_builtin[] =\n")
    out.write("\n".join(f'    "{c_string(line)}\\n"' for line in lines) + ";\n")

print(f"{len(lines) - 1} areas, latest year {max(year for year, _ in latest.values())}")
