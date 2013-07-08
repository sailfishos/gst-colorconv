backend_LTLIBRARIES = libgstcolorconvqcom.la

libgstcolorconvqcom_la_SOURCES = gstcolorconvqcom.c

libgstcolorconvqcom_la_CFLAGS = $(DROID_CFLAGS) \
                                $(GMODULE_CFLAGS) \
                                -I$(top_builddir)/gst/colorconv/

libgstcolorconvqcom_la_LIBADD = $(DROID_LIBS) \
                                $(GMODULE_LIBS)

libgstcolorconvqcom_la_LIBTOOLFLAGS = --tag=disable-static
