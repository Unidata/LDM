# Copyright 2016 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

include_HEADERS		= ulog.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= ulog.c log.c
EXTRA_DIST		= log.h test_gmtime.c
dist_man3_MANS		= ulog.3
CLEANFILES		= test_gmtime

check_PROGRAMS		= test0
test0_SOURCES		= ulog.c
test0_CPPFLAGS		= -DTEST0
test0_LDADD		= ../lib/libldm.la
test_gmtime_SOURCES	= test_gmtime.c

TESTS			= test_gmtime

check-local:	test0
	./test0 -l - 2>/dev/null