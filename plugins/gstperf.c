/* GStreamer
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 */
/**
 * SECTION:element-perf
 *
 * Perf plugin can be used to capture pipeline performance data.  Each
 * second perf plugin sends frames per second and bits per second data
 * using gst_element_post_message.
 */

#include "gstperf.h"

#include <stdio.h>

#include "config.h"

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

enum
{
  PROP_0,
  PROP_PRINT_ARM_LOAD
};

    /* class initialization */
#define gst_perf_parent_class parent_class
G_DEFINE_TYPE (GstPerf, gst_perf, GST_TYPE_BASE_TRANSFORM);

/* The message is variable length depending on configuration */
#define GST_PERF_MSG_MAX_SIZE 4096

#define GST_PERF_BITS_PER_BYTE 8

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
  gst_perf_reset (perf);

  perf->print_arm_load = DEFAULT_PRINT_ARM_LOAD;

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
      perf->print_arm_load = g_value_get_boolean (value);
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
gst_perf_start (GstBaseTransform * trans)
{
  GstPerf *perf = GST_PERF (trans);

  gst_perf_reset (perf);

  perf->error = g_error_new (GST_CORE_ERROR,
      GST_CORE_ERROR_TAG, "Performance Information");
  return TRUE;
}

static gboolean
gst_perf_stop (GstBaseTransform * trans)
{
  GstPerf *perf = GST_PERF (trans);

  gst_perf_reset (perf);

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
    gdouble time_factor;
    guint idx;
    gchar info[GST_PERF_MSG_MAX_SIZE];

    time_factor = 1.0 * diff / GST_SECOND;

    /*Calculate frames per second */
    perf->fps = perf->frame_count / time_factor;

    /*Calculate bits per second */
    perf->bps = perf->byte_count * GST_PERF_BITS_PER_BYTE / time_factor;

    idx =
        g_snprintf (info, GST_PERF_MSG_MAX_SIZE,
        "timestamp: %" GST_TIME_FORMAT "; " "bps: %0.03f; "
        "fps: %0.03f", GST_TIME_ARGS (time), perf->bps, perf->fps);

    gst_perf_reset (perf);
    perf->prev_timestamp = time;

    if (perf->print_arm_load) {
      guint32 cpu_load;
      gst_perf_cpu_get_load (perf, &cpu_load);
      idx = g_snprintf (&info[idx], GST_PERF_MSG_MAX_SIZE - idx,
          "; CPU: %d; ", cpu_load);
    }

    gst_element_post_message (
        (GstElement *) perf,
        gst_message_new_info ((GstObject *) perf, perf->error,
            (const gchar *) info));
  }

  perf->frame_count++;
  perf->byte_count += gst_buffer_get_size (buf);

  return GST_FLOW_OK;
}

static void
gst_perf_reset (GstPerf * perf)
{
  g_return_if_fail (perf);

  perf->prev_timestamp = GST_CLOCK_TIME_NONE;

  perf->frame_count = 0;
  perf->fps = 0.0;

  perf->byte_count = G_GUINT64_CONSTANT (0);
  perf->bps = 0.0;

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
