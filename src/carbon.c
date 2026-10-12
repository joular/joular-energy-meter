/*
 * Copyright (c) 2026, Adel Noureddine.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the
 * GNU General Public License v3.0 only (GPL-3.0-only)
 * which accompanies this distribution, and is available at:
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Author : Adel Noureddine
 */

/* Carbon intensity of electricity: Ember's country averages and the IPCC's energy sources */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PJ_WINDOWS
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#include "app.h"

/* data/carbon-intensity.csv, built in by data/update.py */
extern const char carbon_builtin[];

#define MAX_COUNTRIES 300

static electricity countries[MAX_COUNTRIES];
static int country_count;

/* Life-cycle medians of the IPCC (AR5 WG3 Annex III), which Ember uses too. Oil is Ember's
 * figure for oil and other fossil fuels. */
static const electricity sources[] = {
    { "", "Wind", 0, 11.0 },        { "", "Nuclear", 0, 12.0 },   { "", "Hydro", 0, 24.0 },
    { "", "Geothermal", 0, 38.0 },  { "", "Solar", 0, 48.0 },     { "", "Bioenergy", 0, 230.0 },
    { "", "Gas", 0, 490.0 },        { "", "Oil", 0, 700.0 },      { "", "Coal", 0, 820.0 },
};

/* -------------------------------------------------------------------------------------- */
/* The table                                                                              */
/* -------------------------------------------------------------------------------------- */

/* "code,name,year,grams". A name may hold commas, so year and grams are read from the end. */
static bool parse_row(char *line, electricity *e)
{
    char *name, *year, *grams, *end;

    line[strcspn(line, "\r")] = '\0';
    name = strchr(line, ',');
    grams = strrchr(line, ',');
    if (name == NULL || grams == name) return false;
    *grams++ = '\0';
    year = strrchr(line, ',');
    if (year == name) return false;
    *name++ = '\0';
    *year++ = '\0';

    e->year = (int)strtol(year, &end, 10);
    if (end == year || *end != '\0' || e->year <= 0) return false;
    e->grams = strtod(grams, &end);
    if (end == grams || *end != '\0' || !(e->grams >= 0.0 && e->grams < 10000.0)) return false;
    if (strlen(line) >= sizeof e->code || *name == '\0' || strlen(name) >= sizeof e->name) return false;
    strcpy(e->code, line);
    strcpy(e->name, name);
    return true;
}

/* Rows that don't parse, such as the header, are skipped. Returns the count read. */
static int parse(char *text)
{
    int count = 0;

    for (char *line = strtok(text, "\n"); line && count < MAX_COUNTRIES; line = strtok(NULL, "\n"))
        if (parse_row(line, &countries[count])) count++;
    return count;
}

/* Whole file into text, false if missing or too big */
static bool read_file(const char *path, char *text, size_t n)
{
    FILE *f = fopen(path, "rb");
    size_t got;

    if (f == NULL) return false;
    got = fread(text, 1, n - 1, f);
    text[got] = '\0';
    bool whole = feof(f) && !ferror(f);
    fclose(f);
    return whole;
}

void carbon_load(void)
{
    static const char *const places[] = { "", "../share/joularenergymeter/" };
    static char text[64 * 1024];
    char path[4096];

    for (size_t i = 0; i < sizeof places / sizeof *places && country_count == 0; i++) {
        snprintf(path, sizeof path, "%s%s" CARBON_FILE, platform_program_folder(), places[i]);
        if (read_file(path, text, sizeof text) && (country_count = parse(text)) == 0)
            fprintf(stderr, JOULARENERGYMETER_NAME ": nothing read from %s\n", path);
    }
    if (country_count == 0) {
        snprintf(text, sizeof text, "%s", carbon_builtin);
        country_count = parse(text);
    }
}

int carbon_country_count(void)
{
    return country_count;
}

const electricity *carbon_country(int i)
{
    return i >= 0 && i < country_count ? &countries[i] : NULL;
}

int carbon_source_count(void)
{
    return (int)(sizeof sources / sizeof *sources);
}

const electricity *carbon_source(int i)
{
    return i >= 0 && i < carbon_source_count() ? &sources[i] : NULL;
}

/* -------------------------------------------------------------------------------------- */
/* Where the machine is                                                                   */
/* -------------------------------------------------------------------------------------- */

#ifdef PJ_WINDOWS
/* Windows' time zones each cover several countries, the region setting names one */
static bool local_code(char code[4])
{
    return GetGeoInfoA(GetUserGeoID(GEOCLASS_NATION), GEO_ISO3, code, 4, 0) == 4;
}
#else
/* ISO 3166-1 alpha-2 then alpha-3 of each country: zone.tab has the first, Ember the second */
static const char alpha3[] =
    "ADANDAEAREAFAFGAGATGAIAIAALALBAMARMAOAGOAQATAARARGASASMATAUT"
    "AUAUSAWABWAXALAAZAZEBABIHBBBRBBDBGDBEBELBFBFABGBGRBHBHRBIBDI"
    "BJBENBLBLMBMBMUBNBRNBOBOLBQBESBRBRABSBHSBTBTNBVBVTBWBWABYBLR"
    "BZBLZCACANCCCCKCDCODCFCAFCGCOGCHCHECICIVCKCOKCLCHLCMCMRCNCHN"
    "COCOLCRCRICUCUBCVCPVCWCUWCXCXRCYCYPCZCZEDEDEUDJDJIDKDNKDMDMA"
    "DODOMDZDZAECECUEEESTEGEGYEHESHERERIESESPETETHFIFINFJFJIFKFLK"
    "FMFSMFOFROFRFRAGAGABGBGBRGDGRDGEGEOGFGUFGGGGYGHGHAGIGIBGLGRL"
    "GMGMBGNGINGPGLPGQGNQGRGRCGSSGSGTGTMGUGUMGWGNBGYGUYHKHKGHMHMD"
    "HNHNDHRHRVHTHTIHUHUNIDIDNIEIRLILISRIMIMNININDIOIOTIQIRQIRIRN"
    "ISISLITITAJEJEYJMJAMJOJORJPJPNKEKENKGKGZKHKHMKIKIRKMCOMKNKNA"
    "KPPRKKRKORKWKWTKYCYMKZKAZLALAOLBLBNLCLCALILIELKLKALRLBRLSLSO"
    "LTLTULULUXLVLVALYLBYMAMARMCMCOMDMDAMEMNEMFMAFMGMDGMHMHLMKMKD"
    "MLMLIMMMMRMNMNGMOMACMPMNPMQMTQMRMRTMSMSRMTMLTMUMUSMVMDVMWMWI"
    "MXMEXMYMYSMZMOZNANAMNCNCLNENERNFNFKNGNGANINICNLNLDNONORNPNPL"
    "NRNRUNUNIUNZNZLOMOMNPAPANPEPERPFPYFPGPNGPHPHLPKPAKPLPOLPMSPM"
    "PNPCNPRPRIPSPSEPTPRTPWPLWPYPRYQAQATREREUROROURSSRBRURUSRWRWA"
    "SASAUSBSLBSCSYCSDSDNSESWESGSGPSHSHNSISVNSJSJMSKSVKSLSLESMSMR"
    "SNSENSOSOMSRSURSSSSDSTSTPSVSLVSXSXMSYSYRSZSWZTCTCATDTCDTFATF"
    "TGTGOTHTHATJTJKTKTKLTLTLSTMTKMTNTUNTOTONTRTURTTTTOTVTUVTWTWN"
    "TZTZAUAUKRUGUGAUMUMIUSUSAUYURYUZUZBVAVATVCVCTVEVENVGVGBVIVIR"
    "VNVNMVUVUTWFWLFWSWSMYEYEMYTMYTZAZAFZMZMBZWZWE";

/* "Europe/Paris" from a path ending in zoneinfo/Europe/Paris */
static bool after_zoneinfo(const char *path, char *out, size_t n)
{
    const char *at = strstr(path, "zoneinfo/");

    if (at == NULL) return false;
    snprintf(out, n, "%s", at + strlen("zoneinfo/"));
    return true;
}

/* TZ, else the /etc/localtime link, else the name Debian and FreeBSD keep in a file */
static bool zone_name(char *out, size_t n)
{
    static const char *const named_in[] = { "/etc/timezone", "/var/db/zoneinfo" };
    const char *tz = getenv("TZ");
    char path[PATH_MAX];
    ssize_t len;

    if (tz != NULL && *tz == ':') tz++;
    if (tz != NULL && strchr(tz, '/') != NULL) {
        if (tz[0] == '/') return after_zoneinfo(tz, out, n);
        snprintf(out, n, "%s", tz);
        return true;
    }
    len = readlink("/etc/localtime", path, sizeof path - 1);
    if (len > 0) {
        path[len] = '\0';
        if (after_zoneinfo(path, out, n)) return true;
    }
    for (size_t i = 0; i < sizeof named_in / sizeof *named_in; i++) {
        FILE *f = fopen(named_in[i], "r");
        bool read = f != NULL && fgets(out, (int)n, f) != NULL;

        if (f != NULL) fclose(f);
        out[read ? strcspn(out, "\r\n") : 0] = '\0';
        if (out[0] != '\0') return true;
    }
    return false;
}

static bool local_code(char code[4])
{
    char zone[256], line[512], country[3], name[256];
    bool found = false;
    FILE *f;

    if (!zone_name(zone, sizeof zone) || (f = fopen("/usr/share/zoneinfo/zone.tab", "r")) == NULL) return false;
    /* "FR <TAB> +4852+00220 <TAB> Europe/Paris" */
    while (!found && fgets(line, sizeof line, f) != NULL)
        found = line[0] != '#' && sscanf(line, "%2s %*s %255s", country, name) == 2 && strcmp(name, zone) == 0;
    fclose(f);

    for (const char *p = alpha3; found && *p; p += 5) {
        if (p[0] == country[0] && p[1] == country[1]) {
            snprintf(code, 4, "%.3s", p + 2);
            return true;
        }
    }
    return false;
}
#endif

int carbon_local_country(void)
{
    char code[4];
    int world = 0;

    for (int i = 0; i < country_count; i++)
        if (countries[i].code[0] == '\0') world = i;
    if (!local_code(code)) return world;
    for (int i = 0; i < country_count; i++)
        if (strcmp(countries[i].code, code) == 0) return i;
    return world;
}
