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

GST_DEBUG_CATEGORY_STATIC (colorconv_debug);
#define GST_CAT_DEFAULT colorconv_debug

#define gst_color_conv_debug_init(ignored_parameter)                                      \
  GST_DEBUG_CATEGORY_INIT (colorconv_debug, "colorconv", 0, "colorconv element"); \

GST_BOILERPLATE_FULL (GstColorConv, gst_color_conv, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, gst_color_conv_debug_init);

static void gst_color_conv_finalize (GObject * object);

static void
gst_color_conv_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "HW accelerated colorspace converter",
      "Filter/Converter/Video",
      "HW accelerated colorspace converter",
      "Mohammed Hassan <mohammed.hassan@jollamobile.com>");
}

static void
gst_color_conv_class_init (GstColorConvClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_color_conv_finalize;
}

static void
gst_color_conv_init (GstColorConv * src, GstColorConvClass * gclass)
{
}

static void
gst_color_conv_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}
