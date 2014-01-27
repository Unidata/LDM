# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST              = mcast_down_ldm.hin
BUILT_SOURCES           = mcast_down_ldm.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= mcast_down_ldm.c
lib_la_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog
DISTCLEANFILES          = $(BUILT_SOURCES)

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
mcast_down_ldm.h:	mcast_down_ldm.hin mcast_down_ldm.c

if HAVE_OPMOCK
check_PROGRAMS			= mcast_down_ldm_test
mcast_down_ldm_test_CPPFLAGS	= $(lib_la_CPPFLAGS) @OPMOCK_CPPFLAGS@
mcast_down_ldm_test_LDFLAGS	= @OPMOCK_LDFLAGS@
mcast_down_ldm_test_LDADD	= $(top_builddir)/lib/libldm.la @OPMOCK_LDADD@
TESTS				= mcast_down_ldm_test
endif