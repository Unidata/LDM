# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

bin_PROGRAMS	= delayQueue
AM_CPPFLAGS	= \
            -I$(top_builddir)/log       	-I$(top_srcdir)/log \
            -I$(top_builddir)/misc     		-I$(top_srcdir)/misc
delayQueue_SOURCES	= delayQueue.cpp
delayQueue_LDADD	= $(top_builddir)/lib/libldm.la -lstdc++
TAGS_FILES	= \
            ../log/*.c ../log/*.h \
            ../misc/*.c ../misc/*.h
