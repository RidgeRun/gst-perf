/* GStreamer
 * Copyright (C) 2026 RidgeRun, LLC
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

#include <gst/gst.h>

typedef struct
{
  GMainLoop *loop;
} AppData;

#if GST_CHECK_VERSION (1, 10, 0)
static void
print_perf_info (GstMessage * message)
{
  const GstStructure *details = NULL;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  gdouble bps = 0.0;
  gdouble mean_bps = 0.0;
  gdouble fps = 0.0;
  gdouble mean_fps = 0.0;
  gdouble ds_fps = 0.0;
  gint cpu = 0;
  gboolean has_cpu = FALSE;
  gboolean has_ds_fps = FALSE;

  gst_message_parse_info_details (message, &details);
  if (details == NULL) {
    g_printerr ("perf info from %s did not include details\n",
        GST_MESSAGE_SRC_NAME (message));
    return;
  }

  if (!gst_structure_get_clock_time (details, "timestamp", &timestamp) ||
      !gst_structure_get_double (details, "bps", &bps) ||
      !gst_structure_get_double (details, "mean_bps", &mean_bps) ||
      !gst_structure_get_double (details, "fps", &fps) ||
      !gst_structure_get_double (details, "mean_fps", &mean_fps)) {
    gchar *details_str = gst_structure_to_string (details);

    g_printerr ("perf info from %s has unexpected details: %s\n",
        GST_MESSAGE_SRC_NAME (message), details_str);
    g_free (details_str);
    return;
  }

  has_cpu = gst_structure_get_int (details, "cpu", &cpu);
  has_ds_fps = gst_structure_get_double (details, "ds-fps", &ds_fps);

  g_print ("%s: timestamp=%" GST_TIME_FORMAT
      " bps=%.3f mean_bps=%.3f fps=%.3f mean_fps=%.3f",
      GST_MESSAGE_SRC_NAME (message), GST_TIME_ARGS (timestamp), bps,
      mean_bps, fps, mean_fps);

  if (has_ds_fps) {
    g_print (" ds-fps=%.3f", ds_fps);
  }

  if (has_cpu) {
    g_print (" cpu=%d", cpu);
  }

  g_print ("\n");
}
#else
static void
print_perf_info (GstMessage * message)
{
  GError *error = NULL;
  gchar *debug = NULL;
  const gchar *printable = NULL;

  gst_message_parse_info (message, &error, &debug);

  printable = (debug != NULL) ? debug :
      ((error != NULL) ? error->message : "(no debug)");
  g_print ("%s\n", printable);

  g_clear_error (&error);
  g_free (debug);
}
#endif

static gboolean
on_bus_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  AppData *app = user_data;

  (void) bus;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_INFO:
      print_perf_info (message);
      break;
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      g_printerr ("error from %s: %s\n",
          GST_MESSAGE_SRC_NAME (message), error->message);
      if (debug != NULL) {
        g_printerr ("debug details: %s\n", debug);
      }

      g_clear_error (&error);
      g_free (debug);
      g_main_loop_quit (app->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (app->loop);
      break;
    default:
      break;
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline = NULL;
  GstBus *bus = NULL;
  GMainLoop *loop = NULL;
  AppData app = { 0 };
  GError *error = NULL;
  guint watch_id = 0;
  GstStateChangeReturn state_ret = GST_STATE_CHANGE_FAILURE;
  gint ret = 0;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  if (loop == NULL) {
    g_printerr ("failed to create main loop\n");
    ret = 1;
    goto out;
  }
  app.loop = loop;

  pipeline =
      gst_parse_launch ("videotestsrc is-live=true ! perf name=perf ! fakesink",
      &error);
  if (pipeline == NULL) {
    g_printerr ("failed to create pipeline: %s\n",
        error != NULL ? error->message : "unknown error");
    ret = 1;
    goto out_loop;
  }

  bus = gst_element_get_bus (pipeline);
  if (bus == NULL) {
    g_printerr ("failed to get pipeline bus\n");
    ret = 1;
    goto out_pipeline;
  }

  watch_id = gst_bus_add_watch (bus, on_bus_message, &app);
  gst_object_unref (bus);
  bus = NULL;

  if (watch_id == 0) {
    g_printerr ("failed to add bus watch\n");
    ret = 1;
    goto out_pipeline;
  }

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("failed to set pipeline to PLAYING\n");
    ret = 1;
    goto out_watch;
  }

  g_main_loop_run (loop);

out_watch:
  g_source_remove (watch_id);

out_pipeline:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

out_loop:
  g_main_loop_unref (loop);

out:
  g_clear_error (&error);

  return ret;
}
