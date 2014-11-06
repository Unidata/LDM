# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

bin_PROGRAMS	= mldm_sender
CPPFLAGS	= \
    -I$(top_builddir)/ulog        -I$(top_srcdir)/ulog \
    -I$(top_builddir)/protocol    -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2   -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry    -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_builddir)/misc     	  -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_builddir)/mcast_lib/C -I$(top_srcdir)/mcast_lib/C \
    -I$(top_srcdir)/mcast_lib/C++
mldm_sender_LDADD	= $(top_builddir)/lib/libldm.la \
		          -lstdc++
TAGS_FILES	= \
    ../mcast_lib/*.c ../mcast_lib/*.h \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h
