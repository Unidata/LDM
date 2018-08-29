# Copyright 2015 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SENDER_SRCDIR	= $(top_srcdir)/FMTPv3/sender
AM_CPPFLAGS	= -I$(SENDER_SRCDIR) @GTEST_CPPFLAGS@
ProdIndexDelayQueueTest_SOURCES 	= \
        ProdIndexDelayQueueTest.cpp \
        $(SENDER_SRCDIR)/ProdIndexDelayQueue.cpp
LDFLAGS		= @GTEST_LDFLAGS@ @GTEST_LDADD@

if HAVE_GTEST
check_PROGRAMS	= ProdIndexDelayQueueTest
TESTS		= $(check_PROGRAMS)
endif
