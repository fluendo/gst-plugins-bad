plugin_LTLIBRARIES = \
	libgstvp9.la

libgstvp9_la_SOURCES = \
	gstvp9dec.c \
	gstvp9utils.c \
	plugin.c

libgstvp9_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(VPX_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstvp9_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstbasevideo-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(VPX_LIBS)
libgstvp9_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstvp9_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
	gstvp9dec.h \
	gstvp9utils.h
