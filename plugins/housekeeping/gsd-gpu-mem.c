/*
 * Copyright (C) 2017  Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <glib.h>
#include <dlfcn.h>
#include <math.h>
#include "nvml/include/nvml.h"

typedef nvmlReturn_t (*nvml_init_t) (void);
static nvml_init_t nvml_init;
typedef nvmlReturn_t (*nvml_device_get_handle_by_index_t) (unsigned int, nvmlDevice_t *);
static nvml_device_get_handle_by_index_t nvml_device_get_handle_by_index;
typedef nvmlReturn_t (*nvml_device_get_memory_info_t) (nvmlDevice_t, nvmlMemory_t *);
static nvml_device_get_memory_info_t nvml_device_get_memory_info;
typedef const char* (*nvml_error_string_t) (nvmlReturn_t);
static nvml_error_string_t nvml_error_string;
static void *nvml_handle = NULL;
static gboolean nvml_loaded = FALSE;

static gboolean
nvml_load (void)
{
  char *error;

  if (nvml_loaded)
    goto out;
  nvml_loaded = TRUE;

  nvml_handle = dlopen ("libnvidia-ml.so", RTLD_LAZY);
  error = dlerror();
  if (!nvml_handle)
    {
      g_warning ("Loading NVML: %s", error);
      goto out;
    }

  nvml_init = (nvml_init_t) dlsym (nvml_handle, "nvmlInit");
  nvml_device_get_handle_by_index = (nvml_device_get_handle_by_index_t) dlsym (nvml_handle, "nvmlDeviceGetHandleByIndex");
  nvml_device_get_memory_info = (nvml_device_get_memory_info_t) dlsym (nvml_handle, "nvmlDeviceGetMemoryInfo");
  nvml_error_string = (nvml_error_string_t) dlsym (nvml_handle, "nvmlErrorString");
  error = dlerror();
  if (error)
    {
      g_warning ("Loading NVML: %s", error);
      goto cleanup;
    }
  goto out;

 cleanup:
  dlclose (nvml_handle);
  nvml_handle = NULL;
 out:
  return nvml_handle != NULL;
}

static nvmlReturn_t nvml_status = NVML_ERROR_UNINITIALIZED;
static nvmlDevice_t nvml_device = NULL;

static nvmlReturn_t
nvml_warn (nvmlReturn_t rcode, const gchar *prefix)
{
  if (rcode != NVML_SUCCESS)
    g_warning ("%s: %s", prefix, nvml_error_string (rcode));
  return rcode;
}

static gboolean
nvml_ready (void)
{
  if (!nvml_load ())
    return FALSE;

  if (nvml_status == NVML_ERROR_UNINITIALIZED)
    {
      nvml_status = nvml_warn (nvml_init (), "Initializing NVML");
      if (nvml_status == NVML_SUCCESS)
        nvml_warn (nvml_device_get_handle_by_index (0, &nvml_device), "Getting NVML device 0");
    }

  return nvml_status == NVML_SUCCESS && nvml_device != NULL;
}

static void
nvml_read_mem_info (nvmlMemory_t *mem_info)
{
  if (!nvml_ready ())
    return;

  nvml_device_get_memory_info (nvml_device, mem_info);
}


#include <gio/gdesktopappinfo.h>
#include <libnotify/notify.h>
#include "gsd-gpu-mem.h"

#define POLLING_INTERVAL 30
#define MEM_USAGE_NOTIFY_THRESHOLD 0.80

struct _GsdGpuMem
{
  GObject parent_instance;

  guint timeout_id;

  GDesktopAppInfo *sysmon_app_info;
  NotifyNotification *notification;
};

G_DEFINE_TYPE (GsdGpuMem, gsd_gpu_mem, G_TYPE_OBJECT)

static void
clear_timeout (GsdGpuMem *self)
{
  if (self->timeout_id != 0)
    {
      g_source_remove (self->timeout_id);
      self->timeout_id = 0;
    }
}

static void
notification_closed (GsdGpuMem *self)
{
  g_clear_object (&self->notification);
}

static void
unnotify (GsdGpuMem *self)
{
  if (!self->notification)
    return;

  notify_notification_close (self->notification, NULL);
}

static void
ignore_callback (NotifyNotification *n,
                 const char         *a,
                 GsdGpuMem          *self)
{
  unnotify (self);
  clear_timeout (self);
}

static void
examine_callback (NotifyNotification *n,
                  const char         *a,
                  GsdGpuMem          *self)

{
  unnotify (self);
  g_app_info_launch (G_APP_INFO (self->sysmon_app_info), NULL, NULL, NULL);
}

static void
notify (GsdGpuMem *self, double mem_usage)
{
  const gchar *summary = "Running low on GPU memory";
  const gchar *icon_name = "utilities-system-monitor-symbolic";
  g_autofree gchar *body = g_strdup_printf ("GPU memory usage is at %d%%. "
                                            "You may free up some memory by closing some applications.",
                                            (int) round (mem_usage * 100));
  if (self->notification)
    {
      notify_notification_update (self->notification, summary, body, icon_name);
      notify_notification_show (self->notification, NULL);
      return;
    }

  self->notification = notify_notification_new (summary, body, icon_name);
  g_signal_connect_object (self->notification, "closed",
                           G_CALLBACK (notification_closed),
                           self, G_CONNECT_SWAPPED);

  notify_notification_set_app_name (self->notification, "GPU memory");

  notify_notification_add_action (self->notification,
                                  "ignore",
                                  "Ignore",
                                  (NotifyActionCallback) ignore_callback,
                                  self, NULL);

  if (self->sysmon_app_info)
    notify_notification_add_action (self->notification,
                                    "examine",
                                    "Examine",
                                    (NotifyActionCallback) examine_callback,
                                    self, NULL);

  notify_notification_show (self->notification, NULL);
}

static gboolean
poll_gpu_memory (gpointer user_data)
{
  GsdGpuMem *self = user_data;
  nvmlMemory_t mem_info = { 0 };
  double mem_usage = 0.0;

  nvml_read_mem_info (&mem_info);
  if (mem_info.total > 0)
    mem_usage = (double) mem_info.used / mem_info.total;

  if (mem_usage > MEM_USAGE_NOTIFY_THRESHOLD)
    {
      notify (self, mem_usage);
    }
  else
    {
      unnotify (self);
    }

  return G_SOURCE_CONTINUE;
}

static void
gsd_gpu_mem_init (GsdGpuMem *self)
{
  if (!nvml_ready ())
    return;

  self->timeout_id = g_timeout_add_seconds (POLLING_INTERVAL, poll_gpu_memory, self);
  self->sysmon_app_info = g_desktop_app_info_new ("gnome-system-monitor.desktop");
}

static void
gsd_gpu_mem_finalize (GObject *object)
{
  GsdGpuMem *self = GSD_GPU_MEM (object);

  clear_timeout (self);
  g_clear_object (&self->sysmon_app_info);
  unnotify (self);

  G_OBJECT_CLASS (gsd_gpu_mem_parent_class)->finalize (object);
}

static void
gsd_gpu_mem_class_init (GsdGpuMemClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gsd_gpu_mem_finalize;
}
