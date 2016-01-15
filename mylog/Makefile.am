# Copyright 2016 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST			= mainpage.h
noinst_LTLIBRARIES		= lib.la
lib_la_SOURCES			= mutex.c mutex.h mylog.c mylog.h \
                                  mylog_internal.h
SUBDIRS				= ulog
lib_la_LIBADD			= $(builddir)/ulog/lib.la

if WANT_SLOG
lib_la_SOURCES			+= mylog2slog.c
else
lib_la_SOURCES			+= mylog2ulog.c
lib_la_CPPFLAGS			= -I$(srcdir)/ulog
endif

if HAVE_CUNIT
check_PROGRAMS			= mylog_test
TESTS				= mylog_test

mylog_test_SOURCES	= mylog_test.c $(lib_la_SOURCES)
mylog_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@ $(lib_la_CPPFLAGS)
mylog_test_LDADD	= $(lib_la_LIBADD) $(top_builddir)/lib/libldm.la \
			  @LIBS_CUNIT@

valgrind:       mylog_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
		--leak-check=full --show-reachable=yes ./mylog_test
endif
