# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES	= lib.la
include_HEADERS		= pq.h fbits.h lcm.h
lib_la_SOURCES		= pq.c lcm.c
dist_man3_MANS		= pq.3
lib_la_CPPFLAGS		= \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)
TAGS_FILES		= \
    ../misc/*.c ../misc/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../protocol/*.c ../protocol/*.h
