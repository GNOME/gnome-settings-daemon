/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Bastien Nocera <hadess@hadess.net>
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

#ifndef __GSD_SCREENSAVER_PROXY_MANAGER_H
#define __GSD_SCREENSAVER_PROXY_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_SCREENSAVER_PROXY_MANAGER         (gsd_screensaver_proxy_manager_get_type ())

G_DECLARE_FINAL_TYPE (GsdScreensaverProxyManager, gsd_screensaver_proxy_manager, GSD, SCREENSAVER_PROXY_MANAGER, GObject)

GsdScreensaverProxyManager *gsd_screensaver_proxy_manager_new                 (void);
gboolean                    gsd_screensaver_proxy_manager_start               (GsdScreensaverProxyManager  *manager,
                                                                               GError                     **error);
void                        gsd_screensaver_proxy_manager_stop                (GsdScreensaverProxyManager  *manager);

G_END_DECLS

#endif /* __GSD_SCREENSAVER_PROXY_MANAGER_H */
