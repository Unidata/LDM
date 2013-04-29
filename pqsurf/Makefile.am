# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= surface.h
GDBMLIB			= @GDBMLIB@
PQ_SUBDIR		= @PQ_SUBDIR@
bin_PROGRAMS		= pqsurf
pqsurf_SOURCES		= pqsurf.c surf_split.c
CPPFLAGS		= \
    -I$(top_srcdir)/pqinglib \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)/pqing \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir)
LDADD			= \
    $(top_builddir)/lib/libldm.la \
    -lxml2 \
    $(GDBMLIB) -lm
dist_man1_MANS		= pqsurf.1
TAGS_FILES		= \
    ../pqing/*.c ../pqing/*.h \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

if MAINTAINER
pqsurf.1:	pqsurf.1.in
	$(top_srcdir)/registry/substPaths <$< >$@.tmp
	mv $@.tmp $@
endif
