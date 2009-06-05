
if DMTXPLUGIN
plugindir = $(libdir)/bluetooth/plugins

plugin_LTLIBRARIES = dmtx.la

dmtx_la_SOURCES = main.c manager.h manager.c \
			server.h server.c device.h device.c

LDADD = $(top_builddir)/common/libhelper.a \
		@GDBUS_LIBS@ @GLIB_LIBS@ @DBUS_LIBS@ @BLUEZ_LIBS@
endif

AM_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -fvisibility=hidden -Wno-sign-compare\
		@BLUEZ_CFLAGS@ @DBUS_CFLAGS@ @GLIB_CFLAGS@ @GDBUS_CFLAGS@

INCLUDES = -I$(top_srcdir)/common -I$(top_srcdir)/src

EXTRA_DIST = dmtx.conf

MAINTAINERCLEANFILES = Makefile.in
