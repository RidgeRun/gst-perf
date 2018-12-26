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

#ifndef _GST_PERF_H_
#define _GST_PERF_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS
#define GST_TYPE_PERF \
  (gst_perf_get_type())
#define GST_PERF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PERF,GstPerf))
#define GST_PERF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PERF,GstPerfClass))
#define GST_IS_PERF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PERF))
#define GST_IS_PERF_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PERF))
typedef struct _GstPerf GstPerf;
typedef struct _GstPerfClass GstPerfClass;

struct _GstPerf
{
  GstBaseTransform parent;

  GstPad *sinkpad;
  GstPad *srcpad;
  GError *error;

  GstClockTime prev_timestamp;
  gdouble fps;
  guint32 frame_count;

  gdouble bps;
  guint64 byte_count;

  guint32 prev_cpu_total;
  guint32 prev_cpu_idle;

  /* Properties */
  gboolean print_arm_load;
};

struct _GstPerfClass
{
  GstBaseTransformClass parent_class;
};

GType gst_perf_get_type (void);

G_END_DECLS
#endif
