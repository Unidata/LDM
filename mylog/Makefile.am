# Copyright 2015 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS				= log4c ulog
noinst_LTLIBRARIES		= lib.la

if WANT_LOG4C

lib_la_SOURCES			= mylog2log4c.c mylog2log4c.h
lib_la_LIBADD			= log4c/lib.a

if HAVE_CUNIT
check_PROGRAMS			= mylog2log4c_test
TESTS				= mylog2log4c_test

mylog2log4c_test_SOURCES	= mylog2log4c_test.c mylog2log4c_test.h \
				  mylog2log4c.c mylog2log4c.h
mylog2log4c_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mylog2log4c_test_LDADD		= log4c/lib.la @LIBS_CUNIT@
endif

else

lib_la_SOURCES			= mylog2ulog.c mylog2ulog.h
lib_la_LIBADD			= ulog/lib.a

if HAVE_CUNIT
check_PROGRAMS			= mylog2ulog_test
TESTS				= mylog2ulog_test

mylog2ulog_test_SOURCES		= mylog2ulog_test.c mylog2ulog_test.h \
				  mylog2ulog.c mylog2ulog.h
mylog2ulog_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mylog2ulog_test_LDADD		= ulog/lib.la @LIBS_CUNIT@
endif

endif