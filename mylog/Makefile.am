# Copyright 2015 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

noinst_LTLIBRARIES		= lib.la
# The ulog stuff is required until all the sources are converted to use the
# `mylog` API.
SUBDIRS				= ulog
lib_la_LIBADD			= $(builddir)/ulog/lib.la

if WANT_LOG4C

# Remove "+" when sources are converted
SUBDIRS				+= log4c
lib_la_SOURCES			= mylog2log4c.c mylog2log4c.h
lib_la_CPPFLAGS			= -I$(srcdir)/log4c/src
# Remove "+" when sources are converted
lib_la_LIBADD			+= $(builddir)/log4c/src/log4c/liblog4c.la \
                                  $(builddir)/log4c/src/sd/liblog4c_sd.la

if HAVE_CUNIT
check_PROGRAMS			= mylog2log4c_test
TESTS				= mylog2log4c_test

mylog2log4c_test_SOURCES	= mylog2log4c_test.c mylog2log4c_test.h \
				  mylog2log4c.c mylog2log4c.h
mylog2log4c_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mylog2log4c_test_LDADD		= $(lib_la_LIBADD) @LIBS_CUNIT@

valgrind:       mylog2log4c_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
		--leak-check=full --show-reachable=yes ./mylog2log4c_test
endif

else

lib_la_SOURCES			= mylog2ulog.c mylog2ulog.h
lib_la_CPPFLAGS			= -I$(srcdir)/ulog

if HAVE_CUNIT
check_PROGRAMS			= mylog2ulog_test
TESTS				= mylog2ulog_test

mylog2ulog_test_SOURCES		= mylog2ulog_test.c mylog2ulog_test.h \
				  mylog2ulog.c mylog2ulog.h
mylog2ulog_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mylog2ulog_test_LDADD		= ulog/lib.la @LIBS_CUNIT@
endif

endif
