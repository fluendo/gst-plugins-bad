
plugin_LTLIBRARIES = libgstsnapshot.la

libgstsnapshot_la_SOURCES = \
	gstsnapshot.c
libgstsnapshot_la_CFLAGS = $(GST_CFLAGS)
libgstsnapshot_la_LIBADD = $(GST_LIBS) $(LIBPNG_LIBS)
libgstsnapshot_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@
if !GST_PLUGIN_BUILD_STATIC
libgstsnapshot_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = gstsnapshot.h
