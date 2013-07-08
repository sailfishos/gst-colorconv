plugin_LTLIBRARIES = libgstcolorconv.la

libgstcolorconv_la_SOURCES = plugin.c \
                             gstcolorconvbackend.h \
                             gstcolorconv.c \
                             gstcolorconv.h

libgstcolorconv_la_CFLAGS = $(GST_CFLAGS) \
                            $(DROID_CFLAGS)

libgstcolorconv_la_LIBADD = $(GST_LIBS) \
                            $(DROID_LIBS) \
                            -lgstnativebuffer

libgstcolorconv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcolorconv_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcolorconv.h gstcolorconvbackend.h
