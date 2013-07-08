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

#include <gmodule.h>
#include "gstcolorconvbackend.h"
#include "II420ColorConverter.h"
#include <dlfcn.h>

extern void *android_dlopen (const char *filename, int flag);
extern void *android_dlsym (void *name, const char *symbol);

typedef struct
{
  void *dl;
  II420ColorConverter conv;
} GstColorConvQcom;

static gboolean
qcom_start (gpointer handle)
{
  void *dl;
  GstColorConvQcom *backend = (GstColorConvQcom *) handle;

  dl = android_dlopen ("/system/lib/libI420colorconvert.so", RTLD_LAZY);
  if (!dl) {
    return FALSE;
  }

  void (*init) (II420ColorConverter * converter);

  init = android_dlsym (dl, "getI420ColorConverter");
  if (!init) {
    return FALSE;
  }

  init (&backend->conv);

  backend->conv.openColorConverterLib ();
  handle = backend;

  return TRUE;
}

static gboolean
qcom_stop (gpointer handle)
{
  return TRUE;
}

static int
qcom_get_hal_format (gpointer handle)
{
  /* HAL_PIXEL_FORMAT_NV12_ENCODEABLE */
  return 0x102;
}

static void
qcom_destroy (gpointer handle)
{
  // TODO:
}

gboolean
qcom_convert_from_native (gpointer handle, int width, int height, void *in_data,
    void *out_data)
{
  GstColorConvQcom *backend = (GstColorConvQcom *) handle;
  ARect rect;
  rect.left = 0;
  rect.top = 0;
  rect.right = width;
  rect.bottom = height;

  if (backend->conv.convertDecoderOutputToI420 (in_data, width, height, rect,
          out_data) == 0) {
    return TRUE;
  }

  return FALSE;
}

gboolean
qcom_convert_to_native (gpointer handle, int width, int height, void *in_data,
    void *out_data)
{
  GstColorConvQcom *backend = (GstColorConvQcom *) handle;
  ARect rect;
  int enc_width;
  int enc_height;
  int size;

  if (backend->conv.getEncoderInputBufferInfo (width, height, &enc_width,
          &enc_height, &rect, &size) != 0) {
    return FALSE;
  }

  if (backend->conv.convertI420ToEncoderInput (in_data, width, height,
          enc_width, enc_height, rect, out_data) == 0) {
    return TRUE;
  }

  return FALSE;
}

G_MODULE_EXPORT gboolean
gst_color_conv_backend_get (GstColorConvBackend * backend)
{
  backend->handle = g_malloc (sizeof (GstColorConvQcom));
  backend->start = qcom_start;
  backend->stop = qcom_stop;
  backend->get_hal_format = qcom_get_hal_format;
  backend->destroy = qcom_destroy;
  backend->convert_from_native = qcom_convert_from_native;
  backend->convert_to_native = qcom_convert_to_native;

  return TRUE;
}
