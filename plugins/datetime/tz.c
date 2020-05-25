/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Generic timezone utilities.
 *
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2020 Christian Hergert <chergert@redhat.com>
 *
 * Authors: Hans Petter Jansson <hpj@ximian.com>
 * 
 * Largely based on Michael Fulbright's work on Anaconda.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

#include "tz.h"

static float    convert_pos               (gchar       *pos,
                                           int          digits);
static int      compare_country_names     (const void  *a,
                                           const void  *b);
static void     sort_locations_by_country (GArray      *locations);
static void     load_backward_tz          (TzDB        *tz_db);
static gboolean tz_location_parse         (TzLocation  *loc,
                                           char        *line);
static gboolean parse_word                (char        *line,
                                           char       **save,
                                           const char **strp);

TzDB *
tz_load_db (void)
{
	TzDB *tz_db;
	FILE *tzfile;
	char buf[4096];

	if (!(tzfile = fopen (TZ_DATA_FILE, "r"))) {
		g_warning ("Could not open %s: %s",
			   TZ_DATA_FILE, g_strerror (errno));
		return NULL;
	}

	tz_db = g_new0 (TzDB, 1);
	tz_db->locations = g_array_new (FALSE, FALSE, sizeof (TzLocation));

	while (fgets (buf, sizeof buf, tzfile)) {
		TzLocation loc;

                if (tz_location_parse (&loc, buf))
                        g_array_append_val (tz_db->locations, loc);
	}

	fclose (tzfile);
	
	/* now sort by country */
	sort_locations_by_country (tz_db->locations);

	/* Load up the hashtable of backward links */
	load_backward_tz (tz_db);

	return tz_db;
}

void
tz_db_free (TzDB *db)
{
	g_array_unref (db->locations);
	g_hash_table_destroy (db->backward);
	g_free (db);
}

glong
tz_location_get_utc_offset (TzLocation *loc)
{
	TzInfo *tz_info;
	glong offset;

	tz_info = tz_info_from_location (loc);
	offset = tz_info->utc_offset;
	tz_info_free (tz_info);
	return offset;
}

TzInfo *
tz_info_from_location (TzLocation *loc)
{
	TzInfo *tzinfo;
	time_t curtime;
	struct tm *curzone;
	gchar *tz_env_value;

	g_return_val_if_fail (loc != NULL, NULL);
	g_return_val_if_fail (loc->zone != NULL, NULL);

	tz_env_value = g_strdup (getenv ("TZ"));
	g_setenv ("TZ", loc->zone, 1);

	tzinfo = g_new0 (TzInfo, 1);

	curtime = time (NULL);
	curzone = localtime (&curtime);

#ifndef __sun
	/* Currently this solution doesnt seem to work - I get that */
	/* America/Phoenix uses daylight savings, which is wrong    */
	tzinfo->tzname_normal = tz_intern (curzone->tm_zone);
	if (curzone->tm_isdst) 
		tzinfo->tzname_daylight =
			tz_intern (&curzone->tm_zone[curzone->tm_isdst]);
	else
		tzinfo->tzname_daylight = NULL;

	tzinfo->utc_offset = curzone->tm_gmtoff;
#else
	tzinfo->tzname_normal = NULL;
	tzinfo->tzname_daylight = NULL;
	tzinfo->utc_offset = 0;
#endif

	tzinfo->daylight = curzone->tm_isdst;

	if (tz_env_value)
		g_setenv ("TZ", tz_env_value, 1);
	else
		g_unsetenv ("TZ");

	g_free (tz_env_value);
	
	return tzinfo;
}


void
tz_info_free (TzInfo *tzinfo)
{
	g_free (tzinfo);
}

struct {
	const char *orig;
	const char *dest;
} aliases[] = {
	{ "Asia/Istanbul",  "Europe/Istanbul" },	/* Istanbul is in both Europe and Asia */
	{ "Europe/Nicosia", "Asia/Nicosia" },		/* Ditto */
	{ "EET",            "Europe/Istanbul" },	/* Same tz as the 2 above */
	{ "HST",            "Pacific/Honolulu" },
	{ "WET",            "Europe/Brussels" },	/* Other name for the mainland Europe tz */
	{ "CET",            "Europe/Brussels" },	/* ditto */
	{ "MET",            "Europe/Brussels" },
	{ "Etc/Zulu",       "Etc/GMT" },
	{ "Etc/UTC",        "Etc/GMT" },
	{ "GMT",            "Etc/GMT" },
	{ "Greenwich",      "Etc/GMT" },
	{ "Etc/UCT",        "Etc/GMT" },
	{ "Etc/GMT0",       "Etc/GMT" },
	{ "Etc/GMT+0",      "Etc/GMT" },
	{ "Etc/GMT-0",      "Etc/GMT" },
	{ "Etc/Universal",  "Etc/GMT" },
	{ "PST8PDT",        "America/Los_Angeles" },	/* Other name for the Atlantic tz */
	{ "EST",            "America/New_York" },	/* Other name for the Eastern tz */
	{ "EST5EDT",        "America/New_York" },	/* ditto */
	{ "CST6CDT",        "America/Chicago" },	/* Other name for the Central tz */
	{ "MST",            "America/Denver" },		/* Other name for the mountain tz */
	{ "MST7MDT",        "America/Denver" },		/* ditto */
};

static gboolean
compare_timezones (const char *a,
		   const char *b)
{
	if (g_str_equal (a, b))
		return TRUE;

	if (strchr (b, '/') == NULL) {
		g_autofree char *prefixed = g_strdup_printf ("/%s", b);

		if (g_str_has_suffix (a, prefixed))
			return TRUE;
	}

	return FALSE;
}

const char *
tz_info_get_clean_name (TzDB *tz_db,
			const char *tz)
{
	char *ret;
	const char *timezone;
	guint i;
	gboolean replaced;

	/* Remove useless prefixes */
	if (g_str_has_prefix (tz, "right/"))
		tz = tz + strlen ("right/");
	else if (g_str_has_prefix (tz, "posix/"))
		tz = tz + strlen ("posix/");

	/* Here start the crazies */
	replaced = FALSE;

	for (i = 0; i < G_N_ELEMENTS (aliases); i++) {
		if (compare_timezones (tz, aliases[i].orig)) {
			replaced = TRUE;
			timezone = aliases[i].dest;
			break;
		}
	}

	/* Try again! */
	if (!replaced) {
		/* Ignore crazy solar times from the '80s */
		if (g_str_has_prefix (tz, "Asia/Riyadh") ||
		    g_str_has_prefix (tz, "Mideast/Riyadh")) {
			timezone = "Asia/Riyadh";
			replaced = TRUE;
		}
	}

	if (!replaced)
		timezone = tz;

	ret = g_hash_table_lookup (tz_db->backward, timezone);

        return ret ? ret : timezone;
}

static float
convert_pos (gchar *pos, int digits)
{
	gchar whole[10];
	gchar *fraction;
	gint i;
	float t1, t2;

	if (!pos || strlen(pos) < 4 || digits > 9) return 0.0;

	for (i = 0; i < digits + 1; i++) whole[i] = pos[i];
	whole[i] = '\0';
	fraction = pos + digits + 1;

	t1 = g_strtod (whole, NULL);
	t2 = g_strtod (fraction, NULL);

	if (t1 >= 0.0) return t1 + t2/pow (10.0, strlen(fraction));
	else return t1 - t2/pow (10.0, strlen(fraction));
}

static int
compare_country_names (gconstpointer a,
                       gconstpointer b)
{
	const TzLocation *tza = a;
	const TzLocation *tzb = b;

	return strcmp (tza->zone, tzb->zone);
}


static void
sort_locations_by_country (GArray *locations)
{
        g_array_sort (locations, compare_country_names);
}

static void
load_backward_tz (TzDB *tz_db)
{
	FILE *backward;
	char buf[4096];

	g_assert (tz_db != NULL);
	g_assert (tz_db->backward == NULL);

	tz_db->backward = g_hash_table_new (g_str_hash, g_str_equal);

	if (!(backward = fopen (GNOMECC_DATA_DIR "/datetime/backward", "r"))) {
		g_warning ("Failed to load 'backward' file: %s",
			   g_strerror (errno));
		return;
	}

	while (fgets (buf, sizeof buf, backward)) {
		const char *ignore;
		const char *real;
		const char *alias;
		char *save = NULL;

		/* Skip comments and empty lines */
		if (g_ascii_strncasecmp (buf, "Link\t", 5) != 0)
			continue;

		if (parse_word (buf, &save, &ignore) &&
		    parse_word (NULL, &save, &real) &&
		    parse_word (NULL, &save, &alias)) {
			/* We don't need more than one name for it */
			if (g_str_equal (real, "Etc/UTC") ||
			    g_str_equal (real, "Etc/UCT"))
				real = "Etc/GMT";

			g_hash_table_insert (tz_db->backward,
			                     (char *)tz_intern (alias),
			                     (char *)tz_intern (real));
		}
	}

	fclose (backward);
}

static gboolean
parse_word (char        *line,
            char       **save,
            const char **strp)
{
	char *ret = strtok_r (line, "\t", save);

	if (ret != NULL) {
		*strp = tz_intern (ret);
		return TRUE;
	}

	return FALSE;
}

static gboolean
parse_lat_lng (char    *line,
               char   **save,
               gfloat  *lat,
               gfloat  *lng)
{
	char *ret = strtok_r (line, "\t", save);
	char *p;
	char sign;

	/* The value from zone.tab looks something like "+4230+00131" */
	if (ret == NULL || (ret[0] != '-' && ret[0] != '+'))
		return FALSE;

	/* Advance p to longitude portion */
	for (p = ret+1; *p && *p != '-' && *p != '+'; p++) { /* Do nothing */ }
	sign = *p;
	*p = 0;
	*lat = convert_pos (ret, 2);
	*p = sign;
	*lng = convert_pos (p, 3);

	return TRUE;
}

static gboolean
tz_location_parse (TzLocation *loc,
                   char       *line)
{
	char *save = NULL;

	g_assert (loc != NULL);
	g_assert (line != NULL);

	if (line[0] == '#')
		return FALSE;

	loc->dist = 0;

	return parse_word (line, &save, &loc->country) &&
	       parse_lat_lng (NULL, &save, &loc->latitude, &loc->longitude) &&
	       parse_word (NULL, &save, &loc->zone);
}

static inline gboolean
char_equal_no_case (char a,
                    char b)
{
	/* 32 (0100000) denotes upper vs lowercase in ASCII */
	return (a & ~0100000) == (b & ~0100000);
}

gboolean
tz_location_is_country_code (const TzLocation *loc,
                             const char       *country_code)
{
	const char *z, *c;

	g_assert (loc != NULL);
	g_assert (country_code != NULL);

	z = loc->zone;
	c = country_code;

	for (; *z && *c; z++, c++) {
		if (!char_equal_no_case (*z, *c))
			return FALSE;
	}

	return *z == 0 && *c == 0;
}

void
tz_populate_locations (TzDB       *db,
                       GArray     *locations,
                       const char *country_code)
{
	g_assert (db != NULL);
	g_assert (locations != NULL);

	if (country_code == NULL) {
		if (db->locations->len > 0)
			g_array_append_vals (locations,
			                     &g_array_index (db->locations, TzLocation, 0),
			                     db->locations->len);
		return;
	}

	for (guint i = 0; i < db->locations->len; i++) {
		const TzLocation *loc = &g_array_index (db->locations, TzLocation, i);

		if (tz_location_is_country_code (loc, country_code))
			g_array_append_vals (locations, loc, 1);
	}
}

const char *
tz_intern (const char *str)
{
	static GStringChunk *chunks;

	if (str == NULL)
		return NULL;

	if (chunks == NULL)
		chunks = g_string_chunk_new (4*4096);

	return g_string_chunk_insert_const (chunks, str);
}
