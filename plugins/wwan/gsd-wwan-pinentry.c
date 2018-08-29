/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Purism SPC
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
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 */

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <libmm-glib.h>

#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr-base.h>

#include "gsd-wwan-device.h"
#include "gsd-wwan-pinentry.h"


static gchar *
get_sim_identifier (GsdWwanDevice *device)
{
        MMSim *sim = gsd_wwan_device_get_mm_sim (device);
        char *identifier;

        identifier = mm_sim_dup_operator_name (sim);
        if (identifier)
                return identifier;

        identifier = mm_sim_dup_operator_identifier (sim);
        if (identifier)
                return identifier;

        identifier = mm_sim_dup_identifier (sim);
        if (identifier)
                return identifier;

        return NULL;
}


static GcrPrompt *
create_prompt (GsdWwanDevice *device, const char* msg)
{
        GError *error = NULL;
        GcrPrompt *prompt;
        const gchar *type;
        g_autofree gchar *identifier = NULL;
        g_autofree gchar *description = NULL;
        g_autofree gchar *message = NULL;
        g_autofree gchar *warning = NULL;
        MMModem *modem = gsd_wwan_device_get_mm_modem (device);
        MMModemLock lock;
        /* MM does not support g_autoptr */
        g_autoptr(GObject) retries = NULL;
        guint num;

        identifier = get_sim_identifier (device);
        g_return_val_if_fail (identifier, NULL);
        g_debug ("Creating new PIN/PUK dialog for SIM %s", identifier);

        /* TODO: timeout */
        prompt = GCR_PROMPT (gcr_system_prompt_open (-1, NULL, &error));
        if (!prompt) {
                /* timeout expired */
                if (error->code == GCR_SYSTEM_PROMPT_IN_PROGRESS)
                        g_warning ("The Gcr system prompter was already in use.");
                else {
                        g_warning ("Couldn't create prompt for SIM PIN entry: %s", error->message);
                        g_error_free (error);
                }
                return NULL;
        }

        /* Set up the dialog  */
        gcr_prompt_set_title (prompt, _("Unlock SIM card"));
        gcr_prompt_set_continue_label (prompt, _("OK"));
        gcr_prompt_set_cancel_label (prompt, _("Cancel"));

        lock = mm_modem_get_unlock_required (modem);
        if (lock == MM_MODEM_LOCK_SIM_PIN) {
                type = "PIN";
        } else if (lock == MM_MODEM_LOCK_SIM_PUK) {
                /* type = "PUK"; */
                g_warning ("Handling PUKs not yet suppored");
                return NULL;
        } else {
                g_warning ("Unsupported lock type: %u", lock);
                return NULL;
        }

        description = g_strdup_printf (_("Please provide the %s for SIM card %s"), type, identifier);
        g_return_val_if_fail (description, NULL);
        gcr_prompt_set_description (prompt, description);

        message = g_strdup_printf (_("Enter %s to unlock your SIM card"), type);
        g_return_val_if_fail (message, NULL);
        gcr_prompt_set_message (prompt, message);

        retries = G_OBJECT(mm_modem_get_unlock_retries (modem));
        num = mm_unlock_retries_get (MM_UNLOCK_RETRIES (retries), lock);

        if (msg) {
                warning = g_strdup_printf (_("%s. (You have %u tries left)"), msg, num);
        } else {
                warning = g_strdup_printf (_("You have %u retries left"), num);
        }
        g_return_val_if_fail (warning, NULL);
        gcr_prompt_set_warning (prompt, warning);

        if (lock == MM_MODEM_LOCK_SIM_PIN) {
                /* TODO
                gcr_prompt_set_choice_label (prompt, _("Automatically unlock this SIM card"));
                */
        }
        return prompt;
}


static GcrPrompt *
create_confirm_prompt (GsdWwanDevice *device, const char* msg)
{
        GError *error = NULL;
        GcrPrompt *prompt;
        g_autofree gchar *identifier = NULL;
        /* MM does not support g_autoptr */
        g_autoptr(GObject) retries = NULL;

        identifier = get_sim_identifier (device);
        g_return_val_if_fail (identifier, NULL);
        g_debug ("Creating new confirm for SIM %s", identifier);

        /* TODO: timeout */
        prompt = GCR_PROMPT (gcr_system_prompt_open (-1, NULL, &error));
        if (!prompt) {
                /* timeout expired */
                if (error->code == GCR_SYSTEM_PROMPT_IN_PROGRESS)
                        g_warning ("The Gcr system prompter was already in use.");
                else {
                        g_warning ("Couldn't create prompt for SIM confirm: %s", error->message);
                        g_error_free (error);
                }
                return NULL;
        }

        /* Set up the dialog */
        gcr_prompt_set_title (prompt, _("SIM card unlock error"));
        gcr_prompt_set_continue_label (prompt, _("OK"));
        gcr_prompt_set_message (prompt, _("SIM card unlock error"));
        gcr_prompt_set_description (prompt, msg);
        return prompt;
}


static void
sim_send_pin_ready_cb (MMSim *sim, GAsyncResult *res, GsdWwanDevice *device)
{
        GError *error = NULL;
        const gchar *msg = NULL;
        MMModem *modem = gsd_wwan_device_get_mm_modem (device);

        if (!mm_sim_send_pin_finish (sim, res, &error)) {
                if (g_error_matches (error,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK)) {
                        g_warning ("Next entry will require the PUK.");
                        /* TODO: handle PUK as well */
                        gsd_wwan_pinentry_unlock_sim_error (device,_("Too many incorrect PINs. "
                                                                     "Please contact your provider."));
                        return;
                } else { /* Report error and re-try PIN request */
                        if (g_error_matches (error,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD)) {
                                msg = _("Wrong PIN code");
                        } else {
                                g_dbus_error_strip_remote_error (error);
                                msg = error->message;
                                g_warning ("Got error '%s'", msg);
                        }
                }

                g_warning ("Failed to send PIN to devid: '%s' simid: '%s' : %s",
                           mm_modem_get_device_identifier (modem),
                           mm_sim_get_identifier (sim),
                           error->message);

                gsd_wwan_pinentry_unlock_sim (device, msg);
                g_clear_error (&error);
                return;
        } else {
                g_debug ("Succesfully unlocked %s", mm_sim_get_identifier (sim));
        }
}


static void
send_code_to_sim (GsdWwanDevice *device, const gchar *code, const gchar *new_pin)
{
        MMModem *modem = gsd_wwan_device_get_mm_modem (device);
        MMSim *sim = gsd_wwan_device_get_mm_sim (device);
        MMModemLock lock = mm_modem_get_unlock_required (modem);

        g_return_if_fail (code);
        /* Send the code to ModemManager */
        if (lock == MM_MODEM_LOCK_SIM_PIN) {
                mm_sim_send_pin (sim, code,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)sim_send_pin_ready_cb,
                                 device);
        } else if (lock == MM_MODEM_LOCK_SIM_PUK) {
                g_return_if_fail (new_pin);
                g_assert_not_reached ();
#if 0
                mm_sim_send_puk (info->mm_sim,
                                 code, /* puk */
                                 new_pin, /* new pin */
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)sim_send_puk_ready_cb,
                                 info);
#endif
        } else {
                /* We should never get a prompt for unsupported types */
                g_error ("Unhadled lock type %u", lock);
        }
}


void
gsd_wwan_pinentry_unlock_sim (GsdWwanDevice *device, const char *error_msg)
{
        GError *err = NULL;
        const char *code;
        GcrPrompt *prompt;
        g_autofree gchar *msg = NULL;

        if (error_msg) {
                msg = g_strdup (error_msg);
                g_return_if_fail (msg);
        }

        prompt = create_prompt (device, msg);
        g_return_if_fail (prompt);
        code = gcr_prompt_password_run (prompt, NULL, &err);

        /* Close gcr_prompt as late as possible so the user has a
           chance to see the spinner */
        if (err) {
                g_warning ("Could not get PIN/PUK %s", err->message);
                g_dbus_error_strip_remote_error (err);

                gcr_prompt_close (prompt);
                gsd_wwan_pinentry_unlock_sim_error (device, err->message);
                g_clear_error (&err);
        } else if (code == NULL) {
                g_debug ("Operation was cancelled");

                gcr_prompt_close (prompt);
        } else {
                g_debug("Got PIN/PUK");
                send_code_to_sim (device, code, NULL);

                gcr_prompt_close (prompt);
        }
};


void
gsd_wwan_pinentry_unlock_sim_error (GsdWwanDevice *device, const char *error_msg)
{
        GError *err = NULL;
        GcrPrompt *prompt;
        g_autofree gchar *msg = NULL;

        if (error_msg) {
                msg = g_strdup (error_msg);
                g_return_if_fail (msg);
        }

        prompt = create_confirm_prompt (device, msg);
        g_return_if_fail (prompt);
        gcr_prompt_confirm_run (prompt, NULL, &err);
        if (err) {
                g_warning ("Error in confirm dialog %s", err->message);
                g_clear_error (&err);
        }
        gcr_prompt_close (prompt);
}
