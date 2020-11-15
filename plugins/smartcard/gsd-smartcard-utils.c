/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Red Hat, Inc.
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
 *
 */

#include "config.h"
#include "gsd-smartcard-utils.h"

#include <string.h>

#include <glib.h>
#include <gio/gio.h>

static char *
dashed_string_to_studly_caps (const char *dashed_string)
{
        char *studly_string;
        size_t studly_string_length;
        size_t i;

        i = 0;

        studly_string = g_strdup (dashed_string);
        studly_string_length = strlen (studly_string);

        studly_string[i] = g_ascii_toupper (studly_string[i]);
        i++;

        while (i < studly_string_length) {
                if (studly_string[i] == '-' || studly_string[i] == '_') {
                        memmove (studly_string + i,
                                 studly_string + i + 1,
                                 studly_string_length - i - 1);
                        studly_string_length--;
                        if (g_ascii_isalpha (studly_string[i])) {
                                studly_string[i] = g_ascii_toupper (studly_string[i]);
                        }
                }
                i++;
        }
        studly_string[studly_string_length] = '\0';

        return studly_string;
}

static char *
dashed_string_to_dbus_error_string (const char *dashed_string,
                                    const char *old_prefix,
                                    const char *new_prefix,
                                    const char *suffix)
{
        g_autofree char *studly_suffix = NULL;
        char *dbus_error_string;
        size_t dbus_error_string_length;
        size_t i;

        i = 0;

        if (g_str_has_prefix (dashed_string, old_prefix) &&
            (dashed_string[strlen(old_prefix)] == '-' ||
             dashed_string[strlen(old_prefix)] == '_')) {
                dashed_string += strlen (old_prefix) + 1;
        }

        studly_suffix = dashed_string_to_studly_caps (suffix);
        dbus_error_string = g_strdup_printf ("%s.%s.%s", new_prefix, dashed_string, studly_suffix);
        i += strlen (new_prefix) + 1;

        dbus_error_string_length = strlen (dbus_error_string);

        dbus_error_string[i] = g_ascii_toupper (dbus_error_string[i]);
        i++;

        while (i < dbus_error_string_length) {
                if (dbus_error_string[i] == '_' || dbus_error_string[i] == '-') {
                        dbus_error_string[i] = '.';

                        if (g_ascii_isalpha (dbus_error_string[i + 1])) {
                                dbus_error_string[i + 1] = g_ascii_toupper (dbus_error_string[i + 1]);
                        }
                }

                i++;
        }

        return dbus_error_string;
}

void
gsd_smartcard_utils_register_error_domain (GQuark error_domain,
                                           GType  error_enum)
{
        g_autoptr(GTypeClass) type_class = NULL;
        g_autofree char *type_name = NULL;
        const char *error_domain_string;
        GType type;
        GEnumClass *enum_class;
        guint i;

        error_domain_string = g_quark_to_string (error_domain);
        type_name = dashed_string_to_studly_caps (error_domain_string);
        type = g_type_from_name (type_name);
        type_class = g_type_class_ref (type);
        enum_class = G_ENUM_CLASS (type_class);

        for (i = 0; i < enum_class->n_values; i++) {
                g_autofree char *dbus_error_string = NULL;

                dbus_error_string = dashed_string_to_dbus_error_string (error_domain_string,
                                                                        "gsd",
                                                                        "org.gnome.SettingsDaemon",
                                                                        enum_class->values[i].value_nick);

                g_debug ("%s: Registering dbus error %s", type_name, dbus_error_string);
                g_dbus_error_register_error (error_domain,
                                             enum_class->values[i].value,
                                             dbus_error_string);
        }
}

char *
gsd_smartcard_utils_escape_object_path (const char *unescaped_string)
{
  const char *p;
  char *object_path;
  GString *string;

  g_return_val_if_fail (unescaped_string != NULL, NULL);

  string = g_string_new ("");

  for (p = unescaped_string; *p != '\0'; p++)
    {
      guchar character;

      character = (guchar) * p;

      if (((character >= ((guchar) 'a')) &&
           (character <= ((guchar) 'z'))) ||
          ((character >= ((guchar) 'A')) &&
           (character <= ((guchar) 'Z'))) ||
          ((character >= ((guchar) '0')) && (character <= ((guchar) '9'))))
        {
          g_string_append_c (string, (char) character);
          continue;
        }

      g_string_append_printf (string, "_%x_", character);
    }

  object_path = string->str;

  g_string_free (string, FALSE);

  return object_path;
}

const char *
gsd_smartcard_utils_get_login_token_name (void)
{
        return g_getenv ("PKCS11_LOGIN_TOKEN_NAME");
}
