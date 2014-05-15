# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= pq.hin
BUILT_SOURCES		= pq.h
noinst_LTLIBRARIES	= lib.la
include_HEADERS		= pq.h fbits.h lcm.h
lib_la_SOURCES		= pq.c lcm.c
dist_man3_MANS		= pq.3
lib_la_CPPFLAGS		= \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)/ulog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)
TAGS_FILES		= \
    ../misc/*.c ../misc/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../rpc/*.c ../rpc/*.h

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
pq.h:		pq.hin pq.c
