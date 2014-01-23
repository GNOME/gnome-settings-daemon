/*
 * Copyright © 2013 Red Hat, Inc.
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
 * Author: Joaquim Rocha <jrocha@redhat.com>
 */

#ifndef __GSD_WACOM_BUTTON_EDITOR_H__
#define __GSD_WACOM_BUTTON_EDITOR_H__

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GSD_WACOM_BUTTON_EDITOR_TYPE            (gsd_wacom_button_editor_get_type ())
#define GSD_WACOM_BUTTON_EDITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSD_WACOM_BUTTON_EDITOR_TYPE, GsdWacomButtonEditor))
#define GSD_WACOM_IS_BUTTON_EDITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSD_WACOM_BUTTON_EDITOR_TYPE))
#define GSD_WACOM_BUTTON_EDITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSD_WACOM_BUTTON_EDITOR_TYPE, GsdWacomButtonEditorClass))
#define GSD_WACOM_IS_BUTTON_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSD_WACOM_BUTTON_EDITOR_TYPE))
#define GSD_WACOM_BUTTON_EDITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSD_WACOM_BUTTON_EDITOR_TYPE, GsdWacomButtonEditorClass))

typedef struct _GsdWacomButtonEditor      GsdWacomButtonEditor;
typedef struct _GsdWacomButtonEditorClass GsdWacomButtonEditorClass;
typedef struct _GsdWacomButtonEditorPrivate GsdWacomButtonEditorPrivate;


struct _GsdWacomButtonEditor
{
  GtkGrid parent_instance;

  /*< private >*/
  GsdWacomButtonEditorPrivate *priv;
};

struct _GsdWacomButtonEditorClass
{
  GtkGridClass parent_class;

  void (* button_edited) (void);
  void (* done_editing) (void);
};

GtkWidget    * gsd_wacom_button_editor_new      (void);

void           gsd_wacom_button_editor_set_button (GsdWacomButtonEditor *self,
                                                   GsdWacomTabletButton *button,
                                                   GtkDirectionType      direction);

GsdWacomTabletButton * gsd_wacom_button_editor_get_button (GsdWacomButtonEditor *self,
                                                           GtkDirectionType     *direction);


GType          gsd_wacom_button_editor_get_type (void);

#endif /* __GSD_WACOM_BUTTON_EDITOR_H__ */
