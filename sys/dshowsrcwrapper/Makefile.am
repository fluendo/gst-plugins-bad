SUBDIRS = baseclasses

plugin_LTLIBRARIES = libgstdshowsrcwrapper.la

libgstdshowsrcwrapper_la_SOURCES = \
  gstdshowaudiosrc.cpp \
  gstdshow.cpp \
  gstdshowfakesink.cpp \
  gstdshowvideosrc.h \
  gstdshowsrcwrapper.cpp \
  gstdshowvideosrc.cpp

libgstdshowsrcwrapper_la_CPPFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS) $(DIRECTSHOW_CFLAGS) -I$(top_srcdir)/sys/dshowsrcwrapper/baseclasses
libgstdshowsrcwrapper_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR) -lgstaudio-$(GST_MAJORMINOR) \
	$(DIRECTSHOW_LIBS) $(top_builddir)/sys/dshowsrcwrapper/baseclasses/libdshowbaseclasses.la
libgstdshowsrcwrapper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSHOW_LDFAGS)

noinst_HEADERS = \
  gstdshowaudiosrc.h \
  gstdshowfakesink.h \
  gstdshow.h
