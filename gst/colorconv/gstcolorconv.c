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
#define IS_NATIVE_STRUCTURE(x) (strcmp(gst_structure_get_name (x), GST_NATIVE_BUFFER_NAME) == 0)

#define BACKEND "/usr/lib/gstcolorconv/libgstcolorconvqcom.so"
#define BUFFER_LOCK_USAGE GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_SW_WRITE_OFTEN

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
static gboolean gst_color_conv_accept_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static void gst_color_conv_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static void *gst_color_conv_get_buffer_data (GstBuffer * buffer,
    gboolean * locked);
static gboolean gst_color_conv_unlock_buffer (GstBuffer * buffer,
    gboolean locked);

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
  GstColorConv *conv = GST_COLOR_CONV (object);

  GST_DEBUG_OBJECT (conv, "finalize");

  if (conv->backend) {
    conv->backend->destroy (conv->backend->handle);
    g_free (conv->backend);
    conv->backend = NULL;
  }

  if (conv->mod) {
    if (!g_module_close (conv->mod)) {
      GST_WARNING_OBJECT (conv, "failed to unload backend %s",
          g_module_error ());
    }

    conv->mod = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_color_conv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  int x;
  int len;
  GstColorConv *conv = GST_COLOR_CONV (trans);

  GST_DEBUG_OBJECT (conv, "transform caps %" GST_PTR_FORMAT, caps);

  if (!conv->backend) {
    GST_DEBUG_OBJECT (conv, "no backend loaded");

    return
        gst_caps_make_writable (gst_static_pad_template_get_caps
        (&src_template));
  }

  GstCaps *out_caps =
      gst_caps_make_writable (gst_static_pad_template_get_caps (&src_template));

  len = gst_caps_get_size (out_caps);

  for (x = 0; x < len; x++) {
    GstStructure *s = gst_caps_get_structure (out_caps, x);
    if (IS_NATIVE_STRUCTURE (s)) {
      gst_structure_set (s, "format", G_TYPE_INT,
          conv->backend->get_hal_format (conv->backend->handle), NULL);
    }
  }

  GST_LOG_OBJECT (conv, "returning caps %" GST_PTR_FORMAT, out_caps);

  return out_caps;
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
  void *in_data;
  void *out_data;
  gboolean in_locked;
  gboolean out_locked;
  int width;
  int height;
  GstStructure *s;
  gboolean in_native;
  gboolean out_native;
  gboolean ret;

  GstColorConv *conv = GST_COLOR_CONV (trans);

  GST_DEBUG_OBJECT (conv, "transform");

  in_native = IS_NATIVE_CAPS (inbuf->caps);
  out_native = IS_NATIVE_CAPS (outbuf->caps);

  if (in_native == out_native) {
    return GST_FLOW_OK;
  }

  /* lock */
  in_data = gst_color_conv_get_buffer_data (inbuf, &in_locked);
  if (!in_data) {
    GST_ELEMENT_ERROR (conv, LIBRARY, FAILED,
        ("Could not lock native buffer handle"), (NULL));
    return GST_FLOW_ERROR;
  }

  out_data = gst_color_conv_get_buffer_data (outbuf, &out_locked);
  if (!out_data) {
    gst_color_conv_unlock_buffer (inbuf, in_locked);

    GST_ELEMENT_ERROR (conv, LIBRARY, FAILED,
        ("Could not lock native buffer handle"), (NULL));
    return GST_FLOW_ERROR;
  }

  /* Convert */
  s = gst_caps_get_structure (inbuf->caps, 0);

  if (!gst_structure_get_int (s, "width", &width)) {
    GST_ELEMENT_ERROR (conv, STREAM, FORMAT, ("failed to get width"), (NULL));
    gst_color_conv_unlock_buffer (inbuf, in_locked);
    gst_color_conv_unlock_buffer (outbuf, out_locked);

    return GST_FLOW_ERROR;
  }

  if (!gst_structure_get_int (s, "height", &height)) {
    GST_ELEMENT_ERROR (conv, STREAM, FORMAT, ("failed to get height"), (NULL));
    gst_color_conv_unlock_buffer (inbuf, in_locked);
    gst_color_conv_unlock_buffer (outbuf, out_locked);

    return GST_FLOW_ERROR;
  }

  if (in_native) {
    ret =
        conv->backend->convert_from_native (conv->backend->handle, width,
        height, in_data, out_data);
  } else {
    ret =
        conv->backend->convert_to_native (conv->backend->handle, width, height,
        in_data, out_data);
  }

  if (!ret) {
    GST_ELEMENT_ERROR (conv, LIBRARY, ENCODE, ("failed to convert"), (NULL));
    gst_color_conv_unlock_buffer (inbuf, in_locked);
    gst_color_conv_unlock_buffer (outbuf, out_locked);

    return GST_FLOW_ERROR;
  }

  /* unlock */
  if (!gst_color_conv_unlock_buffer (inbuf, in_locked)) {
    GST_WARNING_OBJECT (conv, "failed to unlock inbuf");
  }

  if (!gst_color_conv_unlock_buffer (outbuf, out_locked)) {
    GST_WARNING_OBJECT (conv, "failed to unlock outbuf");
  }

  return GST_FLOW_OK;
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
    /* We just ref the buffer because we will push it as it is. */
    *buf = gst_buffer_ref (input);
  } else if (in_native) {
    *buf = NULL;
  }

  return GST_FLOW_OK;
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
  GstStructure *in;
  GstStructure *out;
  int width;
  int height;
  int fps_n;
  int fps_d;

  GST_DEBUG_OBJECT (trans, "fixate caps");

  GST_LOG_OBJECT (trans, "caps %" GST_PTR_FORMAT, caps);
  GST_LOG_OBJECT (trans, "othercaps %" GST_PTR_FORMAT, othercaps);

  /* We care about width, height, format and framerate */
  in = gst_caps_get_structure (caps, 0);
  out = gst_caps_get_structure (othercaps, 0);

  if (gst_structure_get_int (in, "width", &width)) {
    gst_structure_set (out, "width", G_TYPE_INT, width, NULL);
  }

  if (gst_structure_get_int (in, "height", &height)) {
    gst_structure_set (out, "height", G_TYPE_INT, height, NULL);
  }

  if (gst_structure_get_fraction (in, "framerate", &fps_n, &fps_d)) {
    gst_structure_set (out, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
  }

  GST_LOG_OBJECT (trans, "caps after fixating %" GST_PTR_FORMAT, othercaps);
}

static void *
gst_color_conv_get_buffer_data (GstBuffer * buffer, gboolean * locked)
{
  GstNativeBuffer *native;
  GstGralloc *gralloc;
  int width;
  int height;
  buffer_handle_t *handle;
  int err;
  void *data;

  if (!IS_NATIVE_CAPS (buffer->caps)) {
    *locked = FALSE;
    return GST_BUFFER_DATA (buffer);
  }

  native = GST_NATIVE_BUFFER (buffer);
  *locked = gst_native_buffer_is_locked (native);
  if (*locked) {
    return GST_BUFFER_DATA (buffer);
  }

  *locked = FALSE;
  gralloc = gst_native_buffer_get_gralloc (native);
  width = gst_native_buffer_get_width (native);
  height = gst_native_buffer_get_height (native);
  handle = gst_native_buffer_get_handle (native);

  err = gralloc->gralloc->lock (gralloc->gralloc,
      *handle, BUFFER_LOCK_USAGE, 0, 0, width, height, &data);

  if (err != 0) {
    return NULL;
  }

  return data;
}

static gboolean
gst_color_conv_unlock_buffer (GstBuffer * buffer, gboolean locked)
{
  int err;
  GstGralloc *gralloc;
  buffer_handle_t *handle;
  GstNativeBuffer *native;

  if (!IS_NATIVE_CAPS (buffer->caps)) {
    return TRUE;
  }

  if (locked) {
    return TRUE;
  }

  native = GST_NATIVE_BUFFER (buffer);
  gralloc = gst_native_buffer_get_gralloc (native);
  handle = gst_native_buffer_get_handle (native);

  err = gralloc->gralloc->unlock (gralloc->gralloc, *handle);

  if (err != 0) {
    return FALSE;
  }

  return TRUE;
}
