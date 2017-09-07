plugin_LTLIBRARIES = \
	libgstvpx.la

libgstvpx_la_SOURCES = \
	gstvp8dec.c \
	gstvp8enc.c \
	gstvp9dec.c \
	gstvpxutils.c \
	plugin.c

libgstvpx_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(VPX_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstvpx_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstbasevideo-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(VPX_LIBS)
libgstvpx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstvpx_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
	gstvp8dec.h \
	gstvp8enc.h \
	gstvp9dec.h \
	gstvpxutils.h
