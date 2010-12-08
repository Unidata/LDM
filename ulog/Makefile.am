# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES	= lib.la
include_HEADERS		= ulog.h
lib_la_SOURCES		= ulog.c log.c
check_PROGRAMS		= test0
test0_SOURCES		= ulog.c
test0_CPPFLAGS		= -DTEST0
test0_LDADD		= ../lib/libldm.la
EXTRA_DIST		= log.h
dist_man3_MANS		= ulog.3

check:	$(test0_LDADD)
	./test0 -l - 2>/dev/null
