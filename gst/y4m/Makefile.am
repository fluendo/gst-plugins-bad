
plugin_LTLIBRARIES = libgsty4mdec.la

libgsty4mdec_la_SOURCES = gsty4mdec.c
libgsty4mdec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgsty4mdec_la_LIBADD = \
	$(GST_BASE_PLUGINS_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) -lgstbase-@GST_MAJORMINOR@ \
	$(GST_LIBS)
libgsty4mdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgsty4mdec_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = gsty4mdec.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsty4m -:SHARED libgsty4m \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsty4m_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsty4m_la_CFLAGS) \
	 -:LDFLAGS $(libgsty4m_la_LDFLAGS) \
	           $(libgsty4m_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
