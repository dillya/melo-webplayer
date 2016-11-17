melolib_LTLIBRARIES = libmelo_webplayer.la

libmelo_webplayer_la_SOURCES = \
	melo_player_webplayer.c \
	melo_browser_webplayer.c \
	melo_browser_youtube.c \
	melo_webplayer.c \
	melo_webplayer_plugin.c

libmelo_webplayer_la_CFLAGS = \
	$(LIBSOUP_CFLAGS) \
	$(LIBMELO_CFLAGS)

libmelo_webplayer_la_LIBADD = \
	$(LIBSOUP_LIBS) \
	$(LIBMELO_LIBS)

noinst_HEADERS = \
	melo_player_webplayer.h \
	melo_browser_webplayer.h \
	melo_browser_youtube.h \
	melo_webplayer.h
