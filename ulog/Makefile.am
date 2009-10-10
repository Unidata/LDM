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
bin_PROGRAMS		= hupsyslog ulogger
dist_bin_SCRIPTS	= newlog
check_PROGRAMS		= test0
test0_SOURCES		= ulog.c
test0_CPPFLAGS		= -DTEST0
LDADD			= lib.la
dist_man3_MANS		= ulog.3
EXTRA_DIST		= log.h

check:	$(LDADD)
	./test0 -l - 2>/dev/null
