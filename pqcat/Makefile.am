# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

PQ_SUBDIR	= @PQ_SUBDIR@
bin_PROGRAMS	= pqcat
AM_CPPFLAGS	= \
    -I$(top_builddir)           -I$(top_srcdir) \
    -I$(top_builddir)/log       -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol  -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry  -I$(top_srcdir)/registry \
    -I$(top_builddir)/pq        -I$(top_srcdir)/pq \
    -I$(top_builddir)/misc      -I$(top_srcdir)/misc
pqcat_LDADD	= $(top_builddir)/lib/libldm.la
dist_man1_MANS	= pqcat.1
TAGS_FILES	= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../log/*.c ../log/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h
