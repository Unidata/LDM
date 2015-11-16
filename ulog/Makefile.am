# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

include_HEADERS		= ulog.h mylog.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= ulog.c log.c
EXTRA_DIST		= log.h
dist_man3_MANS		= ulog.3

test0_SOURCES		= ulog.c
test0_CPPFLAGS		= -DTEST0
test0_LDADD		= ../lib/libldm.la
test_gmtime_SOURCES	= test_gmtime.c

check-local:	$(test0_LDADD)
	./test0 -l - 2>/dev/null
	./test_gmtime

if HAVE_CUNIT
check_PROGRAMS			= mylog2ulog_test mylog2log4c_test

mylog2ulog_test_SOURCES		= mylog2ulog_test.c mylog2ulog_test.h \
				  mylog2ulog.c mylog2ulog.h ulog.c ulog.h
mylog2ulog_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mylog2ulog_test_LDADD		= @LIBS_CUNIT@

mylog2log4c_test_SOURCES	= mylog2log4c_test.c mylog2log4c_test.h \
				  mylog2log4c.c mylog2log4c.h ulog.c ulog.h
mylog2log4c_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mylog2log4c_test_LDADD		= @LIBS_CUNIT@

TESTS				= mylog2ulog_test mylog2log4c_test
endif