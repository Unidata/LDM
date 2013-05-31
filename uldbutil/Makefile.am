# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

bin_PROGRAMS	= uldbutil
CPPFLAGS	= \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
uldbutil_LDADD	= $(top_builddir)/lib/libldm.la
dist_man1_MANS	= uldbutil.1
TAGS_FILES	= \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h
