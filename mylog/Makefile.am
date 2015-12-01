# Copyright 2015 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

noinst_LTLIBRARIES		= lib.la

if WANT_LOG4C
SUBDIRS				= log4c
lib_la_SOURCES			= mylog2log4c.c mylog2log4c.h
lib_la_CPPFLAGS			= -I$(srcdir)/log4c/src
lib_la_LIBADD			= $(builddir)/log4c/src/log4c/liblog4c.la \
                                  $(builddir)/log4c/src/sd/liblog4c_sd.la
else
SUBDIRS				= ulog
lib_la_SOURCES			= mylog2ulog.c mylog2ulog.h
lib_la_CPPFLAGS			= -I$(srcdir)/ulog
lib_la_LIBADD			= $(builddir)/ulog/lib.la
endif

if HAVE_CUNIT
check_PROGRAMS			= mylog_test
TESTS				= mylog_test

mylog_test_SOURCES	= mylog_test.c mylog_test.h $(lib_la_SOURCES)
mylog_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@ $(lib_la_CPPFLAGS)
mylog_test_LDADD	= $(lib_la_LIBADD) @LIBS_CUNIT@

valgrind:       mylog_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
		--leak-check=full --show-reachable=yes ./mylog_test
endif