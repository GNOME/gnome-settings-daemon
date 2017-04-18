/*
 *
 *  gnome-bluetooth - Bluetooth integration for GNOME
 *
 *  Copyright (C) 2012  Bastien Nocera <hadess@hadess.net>
 *  Copyright © 2017  Endless Mobile, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __CC_RFKILL_GLIB_H
#define __CC_RFKILL_GLIB_H

#include <glib-object.h>
#include <gio/gio.h>
#include "rfkill.h"

G_BEGIN_DECLS

#define CC_RFKILL_TYPE_GLIB cc_rfkill_glib_get_type ()
G_DECLARE_FINAL_TYPE (CcRfkillGlib, cc_rfkill_glib, CC_RFKILL, GLIB, GObject)

CcRfkillGlib *cc_rfkill_glib_new               (void);
gboolean      cc_rfkill_glib_open              (CcRfkillGlib  *rfkill,
                                                GError       **error);

gboolean      cc_rfkill_glib_get_rfkill_input_inhibited (CcRfkillGlib        *rfkill);
void          cc_rfkill_glib_set_rfkill_input_inhibited (CcRfkillGlib        *rfkill,
							 gboolean             noinput);

void          cc_rfkill_glib_send_change_all_event        (CcRfkillGlib        *rfkill,
							   guint                rfkill_type,
							   gboolean             enable,
							   GCancellable        *cancellable,
							   GAsyncReadyCallback  callback,
							   gpointer             user_data);

gboolean      cc_rfkill_glib_send_change_all_event_finish (CcRfkillGlib        *rfkill,
							   GAsyncResult        *res,
							   GError             **error);

G_END_DECLS

#endif /* __CC_RFKILL_GLIB_H */
