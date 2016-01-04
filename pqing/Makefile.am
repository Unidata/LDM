# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

CLEANFILES      = pqing.1
EXTRA_DIST		= \
    afos_message.h \
    do.c \
    crc_tab.h \
    faa604_message.h \
    feed.h \
    pqing.1.in \
    rawfile.h \
    wmo_message.h \
    wmo_start.h
PQ_SUBDIR		= @PQ_SUBDIR@
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
AM_CPPFLAGS		= \
    -I$(top_srcdir)/pqinglib \
    -I$(top_srcdir)/mylog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/C++
pqing_LDADD		= \
    do_check.o \
    do_nocheck.o \
    $(top_builddir)/lib/libldm.la
nodist_man1_MANS	= pqing.1
TAGS_FILES		= \
	do.c \
	../pqinglib/*.c ../pqinglib/*.h \
	../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
	../protocol/*.c ../protocol/*.h \
	../protocol2/*.c ../protocol2/*.h \
	../registry/*.c ../registry/*.h \
	../mylog/*.c ../mylog/*.h \
	../misc/*.c ../misc/*.h \
	../rpc/*.c ../rpc/*.h

pqing.1:	$(srcdir)/pqing.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

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
