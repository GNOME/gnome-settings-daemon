/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 David Zeuthen <david@fubar.dk>
 * Copyright (C) 2011 Bastien Nocera <hadess@hadess.net>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsd-datetime-mechanism-debian.h"
#include "gsd-datetime-mechanism.h"

gboolean
_get_using_ntp_debian (DBusGMethodInvocation   *context)
{
        gboolean can_use_ntp;
        gboolean is_using_ntp;

        can_use_ntp = FALSE;
        is_using_ntp = FALSE;

        dbus_g_method_return (context, can_use_ntp, is_using_ntp);
        return TRUE;
}

gboolean
_set_using_ntp_debian  (DBusGMethodInvocation   *context,
                        gboolean                 using_ntp)
{
        GError *error;
        int exit_status;
        char *cmd;

        error = NULL;

        cmd = g_strconcat ("/usr/sbin/update-rc.d ntp ", using_ntp ? "enable" : "disable", NULL);

        if (!g_spawn_command_line_sync (cmd,
                                        NULL, NULL, &exit_status, &error)) {
                GError *error2;
                error2 = g_error_new (GSD_DATETIME_MECHANISM_ERROR,
                                      GSD_DATETIME_MECHANISM_ERROR_GENERAL,
                                      "Error spawning '%s': %s", cmd, error->message);
                g_error_free (error);
                dbus_g_method_return_error (context, error2);
                g_error_free (error2);
                g_free (cmd);
                return FALSE;
        }

        g_free (cmd);

        cmd = g_strconcat ("/usr/sbin/service ntp ", using_ntp ? "restart" : "stop", NULL);;

        if (!g_spawn_command_line_sync (cmd,
                                        NULL, NULL, &exit_status, &error)) {
                GError *error2;
                error2 = g_error_new (GSD_DATETIME_MECHANISM_ERROR,
                                      GSD_DATETIME_MECHANISM_ERROR_GENERAL,
                                      "Error spawning '%s': %s", cmd, error->message);
                g_error_free (error);
                dbus_g_method_return_error (context, error2);
                g_error_free (error2);
                g_free (cmd);
                return FALSE;
        }

        g_free (cmd);

        dbus_g_method_return (context);
        return TRUE;
}

