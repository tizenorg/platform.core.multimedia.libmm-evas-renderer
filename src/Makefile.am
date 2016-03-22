lib_LTLIBRARIES = libmmfevasrenderer.la

includelibmmfevasrendererdir = $(includedir)/mmf

includelibmmfevasrenderer_HEADERS = include/mm_evas_renderer.h

libmmfevasrenderer_la_SOURCES = mm_evas_renderer.c

libmmfevasrenderer_la_CFLAGS =  -I$(srcdir)/include \
			$(MMCOMMON_CFLAGS) \
			$(MEDIAPACKET_CFLAGS) \
			$(DLOG_CFLAGS) \
			$(EVAS_CFLAGS) \
			$(ECORE_CFLAGS) \
			$(GLIB_CFLAGS) \
			-Werror -Wno-deprecated -Wno-deprecated-declarations -Wno-cpp

libmmfevasrenderer_la_LIBADD = $(GST_LIBS) \
		$(MMCOMMON_LIBS) \
		$(MEDIAPACKET_LIBS) \
		$(DLOG_LIBS) \
		$(EVAS_LIBS) \
		$(ECORE_LIBS) \
		$(GLIB_LIBS)
