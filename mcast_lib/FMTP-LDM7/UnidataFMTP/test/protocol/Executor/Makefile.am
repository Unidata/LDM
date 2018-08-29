# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

AM_CPPFLAGS	= -I$(top_srcdir)/protocol
ExecutorTest_SOURCES 	= \
	ExecutorTest.cpp ExecutorTest.h \
        $(top_srcdir)/protocol/Executor.cpp \
        $(top_srcdir)/protocol/Executor.h \
        $(top_srcdir)/protocol/Task.cpp \
        $(top_srcdir)/protocol/Task.h
LDFLAGS		= -lgtest

check_PROGRAMS	= ExecutorTest
TESTS		= $(check_PROGRAMS)
