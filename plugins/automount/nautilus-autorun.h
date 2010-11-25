/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Nautilus
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * Nautilus is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

/* TODO:
 *
 * - automount all user-visible media on startup
 *  - but avoid doing autorun for these
 * - unmount all the media we've automounted on shutdown
 * - finish x-content / * types
 *  - finalize the semi-spec
 *  - add probing/sniffing code
 * - clean up code
 * - implement missing features
 *  - "Open Folder when mounted"
 *  - Autorun spec (e.g. $ROOT/.autostart)
 *
 */

#ifndef NAUTILUS_AUTORUN_H
#define NAUTILUS_AUTORUN_H

#include <gtk/gtk.h>
#include <gio/gio.h>

typedef void (*NautilusAutorunOpenWindow) (GMount *mount, gpointer user_data);

void nautilus_autorun (GMount *mount, GSettings *settings, NautilusAutorunOpenWindow open_window_func, gpointer user_data);

void nautilus_allow_autorun_for_volume (GVolume *volume);
void nautilus_allow_autorun_for_volume_finish (GVolume *volume);

GtkDialog * show_error_dialog (const char *primary_text,
		               const char *secondary_text,
		               GtkWindow *parent);

#endif /* NAUTILUS_AUTORUN_H */
