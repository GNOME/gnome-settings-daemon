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

#include "config.h"
#include <glib/gi18n.h>
#include <math.h>
#include "gsd-enums.h"
#include "gsd-wacom-key-shortcut-button.h"
#include "gsd-wacom-device.h"
#include "gsd-wacom-button-editor.h"

#define BACK_OPACITY                0.8
#define DEFAULT_ROW_SPACING         12
#define ACTION_TYPE_KEY             "action-type"
#define CUSTOM_ACTION_KEY           "custom-action"
#define CUSTOM_ELEVATOR_ACTION_KEY  "custom-elevator-action"
#define OLED_LABEL                  "oled-label"

#define GSD_WACOM_BUTTON_EDITOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GSD_WACOM_BUTTON_EDITOR_TYPE, GsdWacomButtonEditorPrivate))

G_DEFINE_TYPE (GsdWacomButtonEditor, gsd_wacom_button_editor, GTK_TYPE_GRID);

enum {
  BUTTON_EDITED,
  DONE_EDITING,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static struct {
  GsdWacomActionType  action_type;
  const gchar        *action_name;
} action_table[] = {
  { GSD_WACOM_ACTION_TYPE_NONE,           NC_("Wacom action-type", "None")                },
  { GSD_WACOM_ACTION_TYPE_CUSTOM,         NC_("Wacom action-type", "Send Keystroke")      },
  { GSD_WACOM_ACTION_TYPE_SWITCH_MONITOR, NC_("Wacom action-type", "Switch Monitor")      },
  { GSD_WACOM_ACTION_TYPE_HELP,           NC_("Wacom action-type", "Show On-Screen Help") }
};

#define WACOM_C(x) g_dpgettext2(NULL, "Wacom action-type", x)

enum {
  ACTION_NAME_COLUMN,
  ACTION_TYPE_COLUMN,
  ACTION_N_COLUMNS
};

struct _GsdWacomButtonEditorPrivate
{
  GsdWacomTabletButton *button;
  GtkDirectionType direction;
  GtkComboBox *action_combo;
  GtkWidget *shortcut_button;
};

static void
assign_custom_key_to_dir_button (GsdWacomButtonEditor *self,
                                 gchar                *custom_key)
{
  GsdWacomTabletButton *button;
  GtkDirectionType dir;
  char *strs[3];
  char **strv;

  button = self->priv->button;
  dir = self->priv->direction;

  strs[2] = NULL;
  strs[0] = strs[1] = "";
  strv = g_settings_get_strv (button->settings, CUSTOM_ELEVATOR_ACTION_KEY);

  if (g_strv_length (strv) >= 1)
    strs[0] = strv[0];
  if (g_strv_length (strv) >= 2)
    strs[1] = strv[1];

  if (dir == GTK_DIR_UP)
    strs[0] = custom_key;
  else
    strs[1] = custom_key;

  g_settings_set_strv (button->settings,
                       CUSTOM_ELEVATOR_ACTION_KEY,
                       (const gchar * const*) strs);

  g_strfreev (strv);
}

static void
change_button_action_type (GsdWacomButtonEditor *self,
                           GsdWacomActionType    type)
{
  GsdWacomActionType    current_type;
  GsdWacomTabletButton *button;

  button = self->priv->button;

  if (button == NULL)
    return;

  current_type = g_settings_get_enum (self->priv->button->settings, ACTION_TYPE_KEY);

  if (button->type == WACOM_TABLET_BUTTON_TYPE_STRIP ||
      button->type == WACOM_TABLET_BUTTON_TYPE_RING)
    {
      if (type == GSD_WACOM_ACTION_TYPE_NONE)
        assign_custom_key_to_dir_button (self, "");
      else if (type == GSD_WACOM_ACTION_TYPE_CUSTOM)
        {
          guint           keyval;
          GdkModifierType mask;
          char            *custom_key;

          g_object_get (self->priv->shortcut_button,
                        "key-value", &keyval,
                        "key-mods", &mask,
                        NULL);

          mask &= ~GDK_LOCK_MASK;

          custom_key = gtk_accelerator_name (keyval, mask);
          assign_custom_key_to_dir_button (self, custom_key);
          g_settings_set_enum (button->settings, ACTION_TYPE_KEY, type);

          g_free (custom_key);
        }
    }
  else if (current_type != type)
    {
      const char *oled_label;

      switch (type) {
      case GSD_WACOM_ACTION_TYPE_NONE:
          oled_label = "";
          break;
      case GSD_WACOM_ACTION_TYPE_HELP:
          oled_label = C_("Action type", "Show On-Screen Help");
          break;
      case GSD_WACOM_ACTION_TYPE_SWITCH_MONITOR:
          oled_label = C_("Action type", "Switch Monitor");
          break;
      default:
          oled_label = "";
          break;
      };
      g_settings_set_string (button->settings, OLED_LABEL, oled_label);
      g_settings_set_enum (button->settings, ACTION_TYPE_KEY, type);
    }

  gtk_widget_set_visible (self->priv->shortcut_button,
                          type == GSD_WACOM_ACTION_TYPE_CUSTOM);
}

static void
update_action_combo (GsdWacomButtonEditor *self,
                     GsdWacomActionType    new_type)
{
  GtkTreeIter iter;
  GsdWacomActionType type;
  gboolean iter_valid;
  GtkTreeModel *model;

  model = gtk_combo_box_get_model (self->priv->action_combo);

  for (iter_valid = gtk_tree_model_get_iter_first (model, &iter);
       iter_valid;
       iter_valid = gtk_tree_model_iter_next (model, &iter))
    {
      gtk_tree_model_get (model, &iter,
                          ACTION_TYPE_COLUMN, &type,
                          -1);

      if (new_type == type)
        {
          gtk_combo_box_set_active_iter (self->priv->action_combo, &iter);
          break;
        }
    }
}

static void
reset_shortcut_button_label (GsdWacomButtonEditor *self)
{
  gtk_button_set_label (GTK_BUTTON (self->priv->shortcut_button), NC_("keyboard shortcut", "None"));
}

static void
on_key_shortcut_cleared (GsdWacomKeyShortcutButton  *shortcut_button,
                         GsdWacomButtonEditor       *self)
{
  update_action_combo (self, GSD_WACOM_ACTION_TYPE_NONE);

  reset_shortcut_button_label (self);

  g_signal_emit (self, signals[BUTTON_EDITED], 0);
}

static void
on_key_shortcut_edited (GsdWacomKeyShortcutButton  *shortcut_button,
                        GsdWacomButtonEditor       *self)
{
  GsdWacomTabletButton *button;
  GtkDirectionType dir;
  char *custom_key;
  guint keyval;
  GdkModifierType mask;

  button = self->priv->button;

  if (button == NULL)
    return;

  dir = self->priv->direction;

  change_button_action_type (self, GSD_WACOM_ACTION_TYPE_CUSTOM);

  g_object_get (self->priv->shortcut_button,
                "key-value", &keyval,
                "key-mods", &mask,
                NULL);

  if (keyval == 0 && mask == 0)
    reset_shortcut_button_label (self);

  mask &= ~GDK_LOCK_MASK;

  custom_key = gtk_accelerator_name (keyval, mask);

  if (button->type == WACOM_TABLET_BUTTON_TYPE_STRIP ||
      button->type == WACOM_TABLET_BUTTON_TYPE_RING)
    {
      char *strs[3];
      char **strv;

      strs[2] = NULL;
      strs[0] = strs[1] = "";
      strv = g_settings_get_strv (button->settings, CUSTOM_ELEVATOR_ACTION_KEY);

      if (g_strv_length (strv) >= 1)
        strs[0] = strv[0];
      if (g_strv_length (strv) >= 2)
        strs[1] = strv[1];

      if (dir == GTK_DIR_UP)
        strs[0] = custom_key;
      else
        strs[1] = custom_key;

      g_settings_set_strv (button->settings,
                           CUSTOM_ELEVATOR_ACTION_KEY,
                           (const gchar * const*) strs);

      g_strfreev (strv);
    }
  else
    {
      char *oled_label;

      oled_label = gtk_accelerator_get_label (keyval, mask);
      g_settings_set_string (button->settings, OLED_LABEL, oled_label);
      g_free (oled_label);

      g_settings_set_string (button->settings, CUSTOM_ACTION_KEY, custom_key);
    }

  g_free (custom_key);

  g_signal_emit (self, signals[BUTTON_EDITED], 0);
}

static void
on_combo_box_changed (GtkComboBox          *combo,
                      GsdWacomButtonEditor *self)
{
  GsdWacomActionType type;
  GtkTreeModel *model;
  GtkTreeIter iter;

  if (!gtk_combo_box_get_active_iter (combo, &iter))
    return;

  model = gtk_combo_box_get_model (combo);
  gtk_tree_model_get (model, &iter, ACTION_TYPE_COLUMN, &type, -1);

  change_button_action_type (self, type);

  g_signal_emit (self, signals[BUTTON_EDITED], 0);
}

static void
on_finish_button_clicked (GtkWidget *button, GsdWacomButtonEditor *self)
{
  g_signal_emit (self, signals[DONE_EDITING], 0);
}

static gboolean
action_type_is_allowed (GsdWacomTabletButton *button, GsdWacomActionType action_type)
{
  if (button->type != WACOM_TABLET_BUTTON_TYPE_STRIP && button->type != WACOM_TABLET_BUTTON_TYPE_RING)
    return TRUE;

  if (action_type == GSD_WACOM_ACTION_TYPE_NONE || action_type == GSD_WACOM_ACTION_TYPE_CUSTOM)
    return TRUE;

  return FALSE;
}

static void
reset_action_combo_model (GsdWacomButtonEditor *self)
{
  GtkListStore *model;
  GtkTreeIter iter;
  GsdWacomTabletButton *button;
  gint i;

  if (self->priv->button == NULL)
    return;

  button = self->priv->button;

  model = GTK_LIST_STORE (gtk_combo_box_get_model (self->priv->action_combo));

  gtk_list_store_clear (model);

  for (i = 0; i < G_N_ELEMENTS (action_table); i++)
    {
      if (!action_type_is_allowed (button, action_table[i].action_type))
        continue;

      gtk_list_store_append (model, &iter);
      gtk_list_store_set (model, &iter,
                          ACTION_NAME_COLUMN, WACOM_C(action_table[i].action_name),
                          ACTION_TYPE_COLUMN, action_table[i].action_type, -1);
    }
}

static void
update_button (GsdWacomButtonEditor *self)
{
  GsdWacomTabletButton *button;
  GtkDirectionType dir;
  GsdWacomActionType current_type;
  gchar *shortcut = NULL;
  guint keyval;
  GdkModifierType mask;

  button = self->priv->button;

  if (button == NULL)
    return;

  dir = self->priv->direction;

  if (button->type == WACOM_TABLET_BUTTON_TYPE_STRIP ||
      button->type == WACOM_TABLET_BUTTON_TYPE_RING)
    {
      char *str;
      char **strv;

      strv = g_settings_get_strv (button->settings, CUSTOM_ELEVATOR_ACTION_KEY);
      if (strv != NULL)
        {
          if (dir == GTK_DIR_UP)
            str = strv[0];
          else
            str = strv[1];

          shortcut = g_strdup (str);
          if (g_strcmp0 (shortcut, "") == 0)
            current_type = GSD_WACOM_ACTION_TYPE_NONE;
          else
            current_type = GSD_WACOM_ACTION_TYPE_CUSTOM;

          g_strfreev (strv);
        }
      else
        {
          current_type = GSD_WACOM_ACTION_TYPE_NONE;
        }
    }
  else
    {
      current_type = g_settings_get_enum (button->settings, ACTION_TYPE_KEY);
      if (current_type == GSD_WACOM_ACTION_TYPE_CUSTOM)
        shortcut = g_settings_get_string (button->settings, CUSTOM_ACTION_KEY);
    }

  if (shortcut != NULL && current_type == GSD_WACOM_ACTION_TYPE_CUSTOM)
    {
      gtk_accelerator_parse (shortcut, &keyval, &mask);

      g_object_set (self->priv->shortcut_button,
                    "key-value", keyval,
                    "key-mods", mask,
                    NULL);

      g_free (shortcut);
    }
  else
    {
      g_object_set (self->priv->shortcut_button,
                    "key-value", 0,
                    "key-mods", 0,
                    NULL);

      reset_shortcut_button_label (self);
    }

  update_action_combo (self, current_type);
}

static void
gsd_wacom_button_editor_init (GsdWacomButtonEditor *self)
{
  gint i;
  GtkStyleContext *style_context;
  GtkListStore *model;
  GtkTreeIter iter;
  GsdWacomButtonEditorPrivate *priv;
  GtkCellRenderer *renderer;
  GtkWidget *action_combo, *shortcut_button, *finish_button;

  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gtk_style_context_add_class (style_context, "osd");

  priv = GSD_WACOM_BUTTON_EDITOR_GET_PRIVATE (self);
  self->priv = priv;

  model = gtk_list_store_new (ACTION_N_COLUMNS, G_TYPE_STRING, G_TYPE_INT);

  for (i = 0; i < G_N_ELEMENTS (action_table); i++) {
    gtk_list_store_append (model, &iter);
    gtk_list_store_set (model, &iter,
                        ACTION_NAME_COLUMN, WACOM_C(action_table[i].action_name),
                        ACTION_TYPE_COLUMN, action_table[i].action_type, -1);
  }

  action_combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (model));
  self->priv->action_combo = GTK_COMBO_BOX (action_combo);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (action_combo), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (action_combo), renderer,
                                  "text", ACTION_NAME_COLUMN, NULL);

  g_signal_connect (action_combo, "changed",
                    G_CALLBACK (on_combo_box_changed),
                    self);

  gtk_grid_attach (GTK_GRID (self), action_combo, 0, 0, 1, 1);

  shortcut_button = gsd_wacom_key_shortcut_button_new ();
  /* Accept all shortcuts and disable the cancel key (by default: Escape)
   * because we might want to assign it too */
  g_object_set (shortcut_button,
                "mode", GSD_WACOM_KEY_SHORTCUT_BUTTON_MODE_ALL,
                "cancel-key", 0,
                NULL);
  self->priv->shortcut_button = shortcut_button;

  g_signal_connect (shortcut_button, "key-shortcut-cleared",
                    G_CALLBACK (on_key_shortcut_cleared),
                    self);
  g_signal_connect (shortcut_button, "key-shortcut-edited",
                    G_CALLBACK (on_key_shortcut_edited),
                    self);

  gtk_grid_attach (GTK_GRID (self), shortcut_button, 1, 0, 1, 1);

  finish_button = gtk_button_new_with_label (_("Done"));

  g_signal_connect (finish_button, "clicked",
                    G_CALLBACK (on_finish_button_clicked),
                    self);

  gtk_grid_attach (GTK_GRID (self), finish_button, 2, 0, 1, 1);

  gtk_grid_set_row_spacing (GTK_GRID (self), DEFAULT_ROW_SPACING);
  gtk_grid_set_column_spacing (GTK_GRID (self), DEFAULT_ROW_SPACING);

  g_object_set (self,
                "halign", GTK_ALIGN_START,
                "valign", GTK_ALIGN_START,
                NULL);

  gtk_widget_show_all (GTK_WIDGET (self));
  gtk_widget_hide (GTK_WIDGET (self));
}

static gboolean
gsd_wacom_button_editor_key_press (GtkWidget   *widget,
                                   GdkEventKey *event)
{
  GTK_WIDGET_CLASS (gsd_wacom_button_editor_parent_class)->key_press_event (widget, event);

  return FALSE;
}

static void
gsd_wacom_button_editor_class_init (GsdWacomButtonEditorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  signals[BUTTON_EDITED] = g_signal_new ("button-edited",
                                         GSD_WACOM_BUTTON_EDITOR_TYPE,
                                         G_SIGNAL_RUN_LAST,
                                         G_STRUCT_OFFSET (GsdWacomButtonEditorClass, button_edited),
                                         NULL, NULL,
                                         g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);

  signals[DONE_EDITING] = g_signal_new ("done-editing",
                                         GSD_WACOM_BUTTON_EDITOR_TYPE,
                                         G_SIGNAL_RUN_LAST,
                                         G_STRUCT_OFFSET (GsdWacomButtonEditorClass, done_editing),
                                         NULL, NULL,
                                         g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);

  widget_class->key_press_event = gsd_wacom_button_editor_key_press;

  g_type_class_add_private (klass, sizeof (GsdWacomButtonEditorPrivate));
}

void
gsd_wacom_button_editor_set_button (GsdWacomButtonEditor *self,
                                    GsdWacomTabletButton *button,
                                    GtkDirectionType      direction)
{
  gboolean reset = TRUE;

  g_return_if_fail (GSD_WACOM_IS_BUTTON_EDITOR (self));

  if (self->priv->button && button && self->priv->button->type == button->type)
    reset = FALSE;

  self->priv->button = button;
  self->priv->direction = direction;

  if (reset)
    reset_action_combo_model (self);

  update_button (self);
}

GsdWacomTabletButton *
gsd_wacom_button_editor_get_button (GsdWacomButtonEditor *self,
                                    GtkDirectionType     *direction)
{
  g_return_val_if_fail (GSD_WACOM_IS_BUTTON_EDITOR (self), NULL);

  *direction = self->priv->direction;

  return self->priv->button;
}

GtkWidget *
gsd_wacom_button_editor_new (void)
{
  return g_object_new (GSD_WACOM_BUTTON_EDITOR_TYPE, NULL);
}
