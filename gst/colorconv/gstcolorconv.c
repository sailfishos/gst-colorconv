/*
 * Copyright (C) 2013 Jolla LTD.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "gstcolorconv.h"
#include <gst/gstnativebuffer.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (colorconv_debug);
#define GST_CAT_DEFAULT colorconv_debug

#define gst_color_conv_debug_init(ignored_parameter)                                      \
  GST_DEBUG_CATEGORY_INIT (colorconv_debug, "colorconv", 0, "colorconv element"); \

#define IS_NATIVE_CAPS(x) (strcmp(gst_structure_get_name (gst_caps_get_structure (x, 0)), GST_NATIVE_BUFFER_NAME) == 0)

#define BACKEND "/usr/lib/gstcolorconv/libgstcolorconvqcom.so"

GST_BOILERPLATE_FULL (GstColorConv, gst_color_conv, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, gst_color_conv_debug_init);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_NATIVE_BUFFER_NAME ","
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ] ;"
        GST_VIDEO_CAPS_YUV ("{ I420 }")));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_NATIVE_BUFFER_NAME ","
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ] ;"
        GST_VIDEO_CAPS_YUV ("{ I420 }")));

static void gst_color_conv_finalize (GObject * object);
static GstCaps *gst_color_conv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_color_conv_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static gboolean gst_color_conv_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_color_conv_start (GstBaseTransform * trans);
static gboolean gst_color_conv_stop (GstBaseTransform * trans);
static GstFlowReturn gst_color_conv_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_color_conv_prepare_output_buffer (GstBaseTransform *
    trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf);
static void gst_color_conv_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static gboolean gst_color_conv_accept_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static void gst_color_conv_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static void
gst_color_conv_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "HW accelerated colorspace converter",
      "Filter/Converter/Video",
      "HW accelerated colorspace converter",
      "Mohammed Hassan <mohammed.hassan@jollamobile.com>");

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);
}

static void
gst_color_conv_class_init (GstColorConvClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_color_conv_finalize;
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_color_conv_transform_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_color_conv_get_unit_size);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_color_conv_set_caps);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_color_conv_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_color_conv_stop);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_color_conv_transform);
  trans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_color_conv_prepare_output_buffer);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_color_conv_before_transform);
  trans_class->accept_caps = GST_DEBUG_FUNCPTR (gst_color_conv_accept_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_color_conv_fixate_caps);
}

static void
gst_color_conv_init (GstColorConv * conv, GstColorConvClass * gclass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (conv);

  gst_base_transform_set_passthrough (trans, FALSE);
  gst_base_transform_set_in_place (trans, FALSE);

  GST_BASE_TRANSFORM_CLASS (gclass)->passthrough_on_same_caps = FALSE;

  conv->backend = NULL;
  conv->mod = NULL;
}

static void
gst_color_conv_finalize (GObject * object)
{
  // TODO:

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_color_conv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GST_DEBUG_OBJECT (trans, "transform caps %" GST_PTR_FORMAT, caps);

  return
      gst_caps_make_writable (gst_static_pad_template_get_caps (&src_template));
}

static gboolean
gst_color_conv_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size)
{
  int width;
  int height;
  GstVideoFormat fmt;

  GST_DEBUG_OBJECT (trans, "get unit size");

  if (IS_NATIVE_CAPS (caps)) {
    *size = sizeof (buffer_handle_t);
    return TRUE;
  }

  if (!gst_video_format_parse_caps (caps, &fmt, &width, &height)) {
    GST_WARNING_OBJECT (trans, "failed to parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  *size = gst_video_format_get_size (fmt, width, height);

  return TRUE;
}

static gboolean
gst_color_conv_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps)
{
  GST_DEBUG_OBJECT (trans, "set caps");
  GST_LOG_OBJECT (trans, "in %" GST_PTR_FORMAT, incaps);
  GST_LOG_OBJECT (trans, "out %" GST_PTR_FORMAT, outcaps);

  // TODO:

  return TRUE;
}

static gboolean
gst_color_conv_start (GstBaseTransform * trans)
{
  GstColorConv *conv = GST_COLOR_CONV (trans);

  GST_DEBUG_OBJECT (conv, "start");

  if (!conv->mod) {
    conv->mod =
        g_module_open (BACKEND, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
  }

  if (!conv->mod) {
    GST_ELEMENT_ERROR (conv, LIBRARY, INIT,
        ("Failed to load conversion backend: %s", g_module_error ()), (NULL));
    return FALSE;
  }

  if (!conv->backend) {
    _gst_color_conv_backend_get sym;

    if (!g_module_symbol (conv->mod, BACKEND_SYMBOL_NAME, (gpointer *) & sym)) {
      GST_ELEMENT_ERROR (conv, LIBRARY, INIT, ("Invalid conversion backend: %s",
              g_module_error ()), (NULL));
      return FALSE;
    }

    conv->backend = g_malloc (sizeof (GstColorConvBackend));

    if (!sym (conv->backend)) {
      GST_ELEMENT_ERROR (conv, LIBRARY, INIT,
          ("Failed to initialize conversion backend"), (NULL));
      return FALSE;
    }
  }

  if (!conv->backend->start (conv->backend->handle)) {
    GST_ELEMENT_ERROR (conv, LIBRARY, INIT,
        ("Failed to start conversion backend"), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_color_conv_stop (GstBaseTransform * trans)
{
  GstColorConv *conv = GST_COLOR_CONV (trans);

  GST_DEBUG_OBJECT (conv, "stop");

  if (conv->backend) {
    if (!conv->backend->stop (conv->backend->handle)) {
      GST_ELEMENT_ERROR (conv, LIBRARY, SHUTDOWN,
          ("Failed to stop conversion backend"), (NULL));
      return FALSE;
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_color_conv_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GST_DEBUG_OBJECT (trans, "transform");

  // TODO:

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_color_conv_prepare_output_buffer (GstBaseTransform *
    trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf)
{
  gboolean in_native;
  gboolean out_native;

  GST_DEBUG_OBJECT (trans, "prepare output buffer %" GST_PTR_FORMAT, caps);

  in_native = IS_NATIVE_CAPS (input->caps);
  out_native = IS_NATIVE_CAPS (caps);

  if (in_native == out_native) {
    *buf = gst_buffer_ref (input);
  } else {

  }

  //    *buf = gst_color_conv_allocate_native_buffer ();

  // TODO:

  return GST_FLOW_ERROR;
}

static void
gst_color_conv_before_transform (GstBaseTransform * trans, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (trans, "before transform");

  // TODO:
}

static gboolean
gst_color_conv_accept_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstColorConv *conv = GST_COLOR_CONV (trans);

  GST_DEBUG_OBJECT (conv, "accept caps: direction %i, caps %" GST_PTR_FORMAT,
      direction, caps);

  if (IS_NATIVE_CAPS (caps)) {
    int format;
    int hal_format = conv->backend->get_hal_format (conv->backend->handle);
    if (gst_structure_get_int (gst_caps_get_structure (caps, 0), "format",
            &format)) {
      GST_WARNING_OBJECT (trans, "failed to get format");
      return FALSE;
    }

    if (format != hal_format) {
      GST_WARNING_OBJECT (trans,
          "backend format (0x%x) is not similar to caps format (0x%x)",
          hal_format, format);
      return FALSE;
    }
  } else {
    GstVideoFormat fmt;
    if (!gst_video_format_parse_caps (caps, &fmt, NULL, NULL)) {
      GST_WARNING_OBJECT (trans, "failed to parse caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }

    if (fmt != GST_VIDEO_FORMAT_I420) {
      GST_WARNING_OBJECT (trans, "Only I420 is supported");
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_color_conv_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GST_DEBUG_OBJECT (trans, "fixate caps");

  GST_LOG_OBJECT (trans, "caps %" GST_PTR_FORMAT, caps);
  GST_LOG_OBJECT (trans, "othercaps %" GST_PTR_FORMAT, othercaps);
  // TODO:
}
