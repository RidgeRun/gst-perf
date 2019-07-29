/* GStreamer
 * Copyright (C) 2019 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-perf
 *
 * Perf plugin can be used to capture pipeline performance data.  Each
 * second perf plugin sends frames per second and bits per second data
 * using gst_element_post_message.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstperf.h"

#include <stdio.h>
#include <string.h>

/* pad templates */
static GstStaticPadTemplate gst_perf_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_perf_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


GST_DEBUG_CATEGORY_STATIC (gst_perf_debug);
#define GST_CAT_DEFAULT gst_perf_debug

#define DEFAULT_PRINT_ARM_LOAD    FALSE
#define DEFAULT_BITRATE_WINDOW_SIZE    0
#define DEFAULT_BITRATE_INTERVAL    1000

enum
{
  PROP_0,
  PROP_PRINT_ARM_LOAD,
  PROP_BITRATE_WINDOW_SIZE,
  PROP_BITRATE_INTERVAL
};

/* GstPerf signals and args */
enum
{
  SIGNAL_ON_BITRATE,
  LAST_SIGNAL
};

struct _GstPerf
{
  GstBaseTransform parent;

  GstPad *sinkpad;
  GstPad *srcpad;
  GError *error;

  GstClockTime prev_timestamp;
  gdouble fps;
  guint32 frame_count;
  guint64 frame_count_total;

  gdouble bps;
  gdouble mean_bps;
  gdouble *bps_window_buffer;
  guint32 bps_window_size;
  guint32 bps_window_buffer_current;
  guint64 byte_count;
  guint64 byte_count_total;
  guint bps_interval;
  guint bps_running_interval;
  GMutex byte_count_mutex;
  GMutex bps_mutex;
  GMutex mean_bps_mutex;
  guint bps_source_id;

  guint32 prev_cpu_total;
  guint32 prev_cpu_idle;

  /* Properties */
  gboolean print_arm_load;
};

struct _GstPerfClass
{
  GstBaseTransformClass parent_class;
};

    /* class initialization */
#define gst_perf_parent_class parent_class
G_DEFINE_TYPE (GstPerf, gst_perf, GST_TYPE_BASE_TRANSFORM);

/* The message is variable length depending on configuration */
#define GST_PERF_MSG_MAX_SIZE 4096

#define GST_PERF_BITS_PER_BYTE 8

#define GST_PERF_MS_PER_S 1000.0

/* prototypes */
static void gst_perf_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void gst_perf_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_perf_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_perf_start (GstBaseTransform * trans);
static gboolean gst_perf_stop (GstBaseTransform * trans);

static void gst_perf_reset (GstPerf * perf);
static void gst_perf_clear (GstPerf * perf);
static gdouble gst_perf_update_average (guint64 count, gdouble current,
    gdouble old);
static double
gst_perf_update_moving_average (guint64 window_size, gdouble old_average,
    gdouble new_sample, gdouble old_sample);

static gboolean gst_perf_update_bps (void *data);

static guint gst_perf_signals[LAST_SIGNAL] = { 0 };

static void
gst_perf_class_init (GstPerfClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_perf_set_property;
  gobject_class->get_property = gst_perf_get_property;

  g_object_class_install_property (gobject_class, PROP_PRINT_ARM_LOAD,
      g_param_spec_boolean ("print-arm-load", "Print arm load",
          "Print the CPU load info", DEFAULT_PRINT_ARM_LOAD, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_BITRATE_WINDOW_SIZE,
      g_param_spec_uint ("bitrate-window-size",
          "Bitrate moving average window size",
          "Number of samples used for bitrate moving average window size, 0 is all samples",
          0, G_MAXINT, DEFAULT_BITRATE_WINDOW_SIZE, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_BITRATE_INTERVAL,
      g_param_spec_uint ("bitrate-interval",
          "Interval between bitrate calculation in ms",
          "Interval between two calculations in ms, this will run even when no buffers are received",
          0, G_MAXINT, DEFAULT_BITRATE_INTERVAL, G_PARAM_WRITABLE));

  gst_perf_signals[SIGNAL_ON_BITRATE] =
      g_signal_new ("on-bitrate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);

  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_perf_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_perf_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_perf_transform_ip);

  gst_element_class_set_static_metadata (element_class,
      "Performance Identity element", "Generic",
      "Get pipeline performance data",
      "Melissa Montero <melissa.montero@ridgerun.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_perf_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_perf_sink_template));
}

static void
gst_perf_init (GstPerf * perf)
{
  gst_perf_clear (perf);

  perf->print_arm_load = DEFAULT_PRINT_ARM_LOAD;
  perf->bps_window_size = DEFAULT_BITRATE_WINDOW_SIZE;
  perf->bps_interval = DEFAULT_BITRATE_INTERVAL;
  perf->bps_running_interval = DEFAULT_BITRATE_INTERVAL;
  perf->bps_window_buffer_current = 0;

  g_mutex_init (&perf->byte_count_mutex);
  g_mutex_init (&perf->bps_mutex);
  g_mutex_init (&perf->mean_bps_mutex);

  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM_CAST (perf), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM_CAST (perf), TRUE);
}

void
gst_perf_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPerf *perf = GST_PERF (object);

  switch (property_id) {
    case PROP_PRINT_ARM_LOAD:
      GST_OBJECT_LOCK (perf);
      perf->print_arm_load = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (perf);
      break;
    case PROP_BITRATE_WINDOW_SIZE:
      GST_OBJECT_LOCK (perf);
      perf->bps_window_size = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (perf);
      break;
    case PROP_BITRATE_INTERVAL:
      GST_OBJECT_LOCK (perf);
      perf->bps_interval = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (perf);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_perf_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstPerf *perf = GST_PERF (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static gboolean
gst_perf_update_bps (void *data)
{
  guint buffer_current_idx;
  GstPerf *perf;
  guint byte_count;
  gdouble bps, mean_bps;

  g_return_val_if_fail (data, FALSE);

  perf = GST_PERF (data);

  g_mutex_lock (&perf->byte_count_mutex);
  byte_count = perf->byte_count;
  perf->byte_count = G_GUINT64_CONSTANT (0);
  g_mutex_unlock (&perf->byte_count_mutex);

  g_mutex_lock (&perf->mean_bps_mutex);
  mean_bps = perf->mean_bps;
  g_mutex_unlock (&perf->mean_bps_mutex);

  /* Calculate bits per second */
  bps =
      byte_count * GST_PERF_BITS_PER_BYTE / (perf->bps_running_interval /
      GST_PERF_MS_PER_S);

  /* Update bps average */
  if (!perf->bps_window_size) {
    mean_bps = gst_perf_update_average (perf->byte_count_total, bps, mean_bps);
  } else {
    /*
     * Moving average uses a circular buffer, get index for next value which
     * is the oldest sample, this is the same as the value were the new sample
     * is to be stored
     */
    buffer_current_idx = (perf->byte_count_total) % perf->bps_window_size;

    mean_bps =
        gst_perf_update_moving_average (perf->bps_window_size, mean_bps,
        bps, perf->bps_window_buffer[buffer_current_idx]);

    perf->bps_window_buffer[buffer_current_idx] = bps;
  }
  g_mutex_lock (&perf->mean_bps_mutex);
  perf->mean_bps = mean_bps;
  g_mutex_unlock (&perf->mean_bps_mutex);

  g_mutex_lock (&perf->bps_mutex);
  perf->bps = bps;
  g_mutex_unlock (&perf->bps_mutex);

  perf->byte_count_total++;

  g_signal_emit_by_name (perf, "on-bitrate", mean_bps);

  return TRUE;
}

static gboolean
gst_perf_start (GstBaseTransform * trans)
{
  GstPerf *perf = GST_PERF (trans);

  gst_perf_clear (perf);

  /* If window size is different from all samples allocate the needed memory */
  if (perf->bps_window_size) {
    perf->bps_window_buffer =
        g_malloc0 ((perf->bps_window_size) * sizeof (gdouble));

    if (!perf->bps_window_buffer) {
      GST_ERROR_OBJECT (perf, "Unable to allocate memory");
      return FALSE;
    }
  }

  perf->bps_running_interval = perf->bps_interval;

  perf->bps_source_id =
      g_timeout_add (perf->bps_interval, gst_perf_update_bps, perf);

  perf->error = g_error_new (GST_CORE_ERROR,
      GST_CORE_ERROR_TAG, "Performance Information");
  return TRUE;
}

static gboolean
gst_perf_stop (GstBaseTransform * trans)
{
  GstPerf *perf = GST_PERF (trans);

  gst_perf_clear (perf);

  g_free (perf->bps_window_buffer);

  g_source_remove (perf->bps_source_id);

  if (perf->error)
    g_error_free (perf->error);

  return TRUE;
}

static gboolean
gst_perf_cpu_get_load (GstPerf * perf, guint32 * cpu_load)
{
  gboolean cpu_load_found = FALSE;
  guint32 user, nice, sys, idle, iowait, irq, softirq, steal;
  guint32 total = 0;
  guint32 diff_total, diff_idle;
  gchar name[4];
  FILE *fp;

  /* Default value in case of failure */
  *cpu_load = -1;

  /* Read the overall system information */
  fp = fopen ("/proc/stat", "r");

  if (fp == NULL) {
    GST_ERROR ("/proc/stat not found");
    goto cpu_failed;
  }
  /* Scan the file line by line */
  while (fscanf (fp, "%4s %d %d %d %d %d %d %d %d", name, &user, &nice,
          &sys, &idle, &iowait, &irq, &softirq, &steal) != EOF) {
    if (strcmp (name, "cpu") == 0) {
      cpu_load_found = TRUE;
      break;
    }
  }

  fclose (fp);

  if (!cpu_load_found) {
    goto cpu_failed;
  }
  GST_DEBUG ("CPU stats-> user: %d; nice: %d; sys: %d; idle: %d "
      "iowait: %d; irq: %d; softirq: %d; steal: %d",
      user, nice, sys, idle, iowait, irq, softirq, steal);

  /*Calculate the total CPU time */
  total = user + nice + sys + idle + iowait + irq + softirq + steal;
  /*Calculate the CPU usage since last time we checked */
  diff_idle = idle - perf->prev_cpu_idle;
  diff_total = total - perf->prev_cpu_total;
  if (diff_total) {
    /*Get a rounded result */
    *cpu_load = (1000 * (diff_total - diff_idle) / diff_total + 5) / 10;
  } else {
    *cpu_load = 0;
  }
  /*Remember the total and idle CPU for the next check */
  perf->prev_cpu_total = total;
  perf->prev_cpu_idle = idle;
  return TRUE;

cpu_failed:
  GST_ERROR ("Failed to get the CPU load");
  return FALSE;
}

static GstFlowReturn
gst_perf_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstPerf *perf = GST_PERF (trans);
  GstClockTime time = gst_util_get_timestamp ();
  GstClockTime diff = GST_CLOCK_DIFF (perf->prev_timestamp, time);

  if (!GST_CLOCK_TIME_IS_VALID (perf->prev_timestamp) ||
      (GST_CLOCK_TIME_IS_VALID (time) && diff >= GST_SECOND)) {
    gdouble time_factor, fps;
    guint idx;
    gchar info[GST_PERF_MSG_MAX_SIZE];
    gboolean print_arm_load;
    gdouble bps, mean_bps;

    time_factor = 1.0 * diff / GST_SECOND;

    /*Calculate frames per second */
    fps = perf->frame_count / time_factor;

    /*Update fps average */
    perf->fps =
        gst_perf_update_average (perf->frame_count_total, fps, perf->fps);
    perf->frame_count_total++;

    g_mutex_lock (&perf->bps_mutex);
    bps = perf->bps;
    g_mutex_unlock (&perf->bps_mutex);

    g_mutex_lock (&perf->mean_bps_mutex);
    mean_bps = perf->mean_bps;
    g_mutex_unlock (&perf->mean_bps_mutex);

    idx =
        g_snprintf (info, GST_PERF_MSG_MAX_SIZE,
        "perf: %s; timestamp: %" GST_TIME_FORMAT "; "
        "bps: %0.03f; mean_bps: %0.03f; " "fps: %0.03f; mean_fps: %0.03f",
        GST_OBJECT_NAME (perf), GST_TIME_ARGS (time), bps, mean_bps,
        fps, perf->fps);

    gst_perf_reset (perf);
    perf->prev_timestamp = time;

    GST_OBJECT_LOCK (perf);
    print_arm_load = perf->print_arm_load;
    GST_OBJECT_UNLOCK (perf);

    if (print_arm_load) {
      guint32 cpu_load;
      gst_perf_cpu_get_load (perf, &cpu_load);
      idx = g_snprintf (&info[idx], GST_PERF_MSG_MAX_SIZE - idx,
          "; cpu: %d; ", cpu_load);
    }

    gst_element_post_message (
        (GstElement *) perf,
        gst_message_new_info ((GstObject *) perf, perf->error,
            (const gchar *) info));
  }

  perf->frame_count++;
  g_mutex_lock (&perf->byte_count_mutex);
  perf->byte_count += gst_buffer_get_size (buf);
  g_mutex_unlock (&perf->byte_count_mutex);

  return GST_FLOW_OK;
}

static gdouble
gst_perf_update_average (guint64 count, gdouble current, gdouble old)
{
  gdouble ret = 0;

  if (count != 0) {
    ret = ((count - 1) * old + current) / count;
  }

  return ret;
}

static gdouble
gst_perf_update_moving_average (guint64 window_size, gdouble old_average,
    gdouble new_sample, gdouble old_sample)
{
  gdouble ret = 0;

  if (window_size != 0) {
    ret = (old_average * window_size - old_sample + new_sample) / window_size;
  }

  return ret;
}

static void
gst_perf_reset (GstPerf * perf)
{
  g_return_if_fail (perf);

  perf->frame_count = 0;
}

static void
gst_perf_clear (GstPerf * perf)
{
  g_return_if_fail (perf);

  gst_perf_reset (perf);

  perf->fps = 0.0;
  perf->frame_count_total = G_GUINT64_CONSTANT (0);

  perf->mean_bps = 0.0;
  perf->byte_count_total = G_GUINT64_CONSTANT (0);
  perf->byte_count = G_GUINT64_CONSTANT (0);

  perf->prev_timestamp = GST_CLOCK_TIME_NONE;
  perf->prev_cpu_total = 0;
  perf->prev_cpu_idle = 0;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  GST_DEBUG_CATEGORY_INIT (gst_perf_debug, "perf", 0,
      "Debug category for perf element");

  return gst_element_register (plugin, "perf", GST_RANK_NONE, GST_TYPE_PERF);
}

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://www.ridgerun.com"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    perf,
    "Get pipeline performance data",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
