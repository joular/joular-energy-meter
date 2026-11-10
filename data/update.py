#!/usr/bin/env python3
# Rebuilds data/carbon-intensity.csv and data/carbon-intensity-monthly.csv, and their built-in
# copy, src/carbon_data.c, from Ember's yearly and monthly electricity data (CC BY 4.0,
# https://ember-energy.org/data/yearly-electricity-data/ and .../monthly-electricity-data/): the
# latest carbon intensity of each country and of the world, and of each calendar month where
# Ember has monthly figures (fewer countries).
#
# Ember updates its data twice a month, new yearly figures come once a year. Run from anywhere:
#   python3 data/update.py                        downloads Ember's files
#   python3 data/update.py YEARLY.csv MONTHLY.csv uses files already downloaded

import csv
import io
import pathlib
import sys
import unicodedata
import urllib.request

URL = "https://files.ember-energy.org/public-downloads/generation/outputs/release_generation_yearly_global.csv"
MONTHLY_URL = "https://files.ember-energy.org/public-downloads/generation/outputs/release_generation_monthly_global.csv"
ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = "ISO 3 code,Area,Year,Emissions intensity (gCO2e/kWh)"
MONTHLY_HEADER = "ISO 3 code,Year,Month,Emissions intensity (gCO2e/kWh)"

def rows(argument, url):
    if argument < len(sys.argv):
        return csv.DictReader(open(sys.argv[argument], encoding="utf-8", newline=""))
    return csv.DictReader(io.TextIOWrapper(urllib.request.urlopen(url), encoding="utf-8", newline=""))

# The whole generation of a country or of the world. A few rows are negative, from bad data:
# the period before is used.
def kept(row):
    grams = row["Emissions intensity (gCO2e/kWh)"]
    return (row["Electricity source"] == "Total generation" and grams and float(grams) > 0
            and (row["Area type"] == "Country or economy" or row["Area"] == "World"))

latest = {}
for row in rows(1, URL):
    if not kept(row):
        continue
    key = (row["ISO 3 code"], row["Area"])
    if key not in latest or int(row["Year"]) > latest[key][0]:
        latest[key] = (int(row["Year"]), float(row["Emissions intensity (gCO2e/kWh)"]))

# Each calendar month from the latest year that has it, by country code ("" for the world)
months = {}
for row in rows(2, MONTHLY_URL):
    if not kept(row):
        continue
    year, month = int(row["Date"][:4]), int(row["Date"][5:7])
    known = months.setdefault(row["ISO 3 code"], {})
    if month not in known or year > known[month][0]:
        known[month] = (year, float(row["Emissions intensity (gCO2e/kWh)"]))

# World first, then by name, Türkiye among the T's
def order(item):
    name = item[0][1]
    return name != "World", unicodedata.normalize("NFKD", name).encode("ascii", "ignore")

lines = [HEADER]
for (code, name), (year, grams) in sorted(latest.items(), key=order):
    name.encode("latin-1")   # fails on a letter the program's font doesn't have
    lines.append(f"{code},{name},{year},{grams:.1f}")

(ROOT / "data" / "carbon-intensity.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")

# Only the areas of the yearly table, all twelve months or none
codes = {code for code, _ in latest}
monthly = [MONTHLY_HEADER]
for code in sorted(months):
    if code in codes and len(months[code]) == 12:
        for month, (year, grams) in sorted(months[code].items()):
            monthly.append(f"{code},{year},{month},{grams:.1f}")
(ROOT / "data" / "carbon-intensity-monthly.csv").write_text("\n".join(monthly) + "\n", encoding="utf-8")

# Octal escapes keep the C file ASCII
def c_string(line):
    return "".join(chr(b) if 32 <= b < 127 and b not in b'"\\' else f"\\{b:03o}" for b in line.encode("utf-8"))

def c_array(name, table):
    return (f"const char {name}[] =\n"
            + "\n".join(f'    "{c_string(line)}\\n"' for line in table) + ";\n")

with open(ROOT / "src" / "carbon_data.c", "w", encoding="ascii") as out:
    out.write("/* Made by data/update.py from Ember's yearly and monthly electricity data (CC BY 4.0), the\n"
              " * same as data/carbon-intensity.csv and data/carbon-intensity-monthly.csv: used when those\n"
              " * files aren't installed */\n\n")
    out.write(c_array("carbon_builtin", lines) + "\n" + c_array("carbon_monthly_builtin", monthly))

print(f"{len(lines) - 1} areas, latest year {max(year for year, _ in latest.values())}, "
      f"{(len(monthly) - 1) // 12} with monthly figures")
