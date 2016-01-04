# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

bin_PROGRAMS	= uldbutil
AM_CPPFLAGS	= \
    -I$(top_srcdir)/mylog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/C++
uldbutil_LDADD	= $(top_builddir)/lib/libldm.la
dist_man1_MANS	= uldbutil.1
TAGS_FILES	= \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../mylog/*.c ../mylog/*.h \
    ../misc/*.c ../misc/*.h
