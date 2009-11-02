# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    afos_message.h \
    do.c \
    dtime.h \
    tokens.h \
    wmo_header.h \
    xbuf.h \
    crc_tab.h \
    faa604_message.h \
    feed.h \
    rawfile.h \
    wmo_message.h \
    wmo_start.h \
    xbuf.h
PQ_SUBDIR		= @PQ_SUBDIR@
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= dtime.c wmo_header.c tokens.c xbuf.c
bin_PROGRAMS		= pqing
pqing_SOURCES		= \
    afos_message.c \
    faa604_message.c \
    feed.c \
    file.c \
    fxbuf.c \
    pqing.c \
    rawfile.c \
    tty.c \
    wmo_message.c \
    wmo_start.c
CPPFLAGS		= \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
pqing_LDADD		= \
    do_check.o \
    do_nocheck.o \
    lib.la \
    ../$(PQ_SUBDIR)/lib.la \
    ../protocol/lib.la \
    ../ulog/lib.la \
    ../misc/lib.la \
    ../rpc/lib.la \
    -lm
dist_man1_MANS		= pqing.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

do_check.o:	do.c
	$(COMPILE) -DSCAN_CHECK -c $(srcdir)/do.c && mv do.o $@;
do_nocheck.o:	do.c
	$(COMPILE) -USCAN_CHECK -c $(srcdir)/do.c && mv do.o $@;

install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f dds && ln -s $(bin_PROGRAMS) dds
	cd $(DESTDIR)$(bindir) && rm -f pps && ln -s $(bin_PROGRAMS) pps
	cd $(DESTDIR)$(bindir) && rm -f ids && ln -s $(bin_PROGRAMS) ids
	cd $(DESTDIR)$(bindir) && rm -f hds && ln -s $(bin_PROGRAMS) hds
	cd $(DESTDIR)$(bindir) && rm -f hrs && ln -s $(bin_PROGRAMS) hrs
	cd $(DESTDIR)$(bindir) && rm -f ddplus && ln -s $(bin_PROGRAMS) ddplus
	cd $(DESTDIR)$(bindir) && rm -f feedtest && ln -s $(bin_PROGRAMS) \
	    feedtest
	cd $(DESTDIR)$(bindir) && rm -f afos && ln -s $(bin_PROGRAMS) afos

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/dds
	rm -f $(DESTDIR)$(bindir)/pps
	rm -f $(DESTDIR)$(bindir)/ids
	rm -f $(DESTDIR)$(bindir)/hds
	rm -f $(DESTDIR)$(bindir)/hrs
	rm -f $(DESTDIR)$(bindir)/ddplus
	rm -f $(DESTDIR)$(bindir)/feedtest
	rm -f $(DESTDIR)$(bindir)/afos
