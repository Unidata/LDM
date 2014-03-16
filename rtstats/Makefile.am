# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= rtstats.1.in
CLEANFILES      	= rtstats.1
PQ_SUBDIR		= @PQ_SUBDIR@

bin_PROGRAMS		= rtstats
rtstats_SOURCES		= ldmsend.c binstats.c filel.c rtstats.c
CPPFLAGS		= \
    -I$(top_srcdir)/ulog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
rtstats_LDADD		= $(top_builddir)/lib/libldm.la
nodist_man1_MANS	= rtstats.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../registry/*.c ../registry/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

rtstats.1:	$(srcdir)/rtstats.1.in
	../regutil/substPaths <$< >$@.tmp
	mv $@.tmp $@
