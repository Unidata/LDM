# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= mcast.cpp mcast.h \
			  PerProdNotifier.cpp PerProdNotifier.h
CPPFLAGS	        = -I$(top_builddir) -I$(top_srcdir) \
                          -I$(top_builddir)/pq -I$(top_srcdir)/pq \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
                          -I$(top_builddir)/registry -I$(top_srcdir)/registry \
                          -I$(top_builddir)/ulog -I$(top_srcdir)/ulog \
                          -I$(srcdir)/../vcmtp/VCMTPv3 \
                          -I$(srcdir)/../vcmtp/VCMTPv3/fatma \
                          -I$(srcdir)/../vcmtp/VCMTPv3/shawn \
                          -I$(srcdir)/../vcmtp/VCMTPv3/shawn/recv \
                          -I$(top_builddir)/misc -I$(top_srcdir)/misc \
                          -I$(top_srcdir)/rpc

if HAVE_OPMOCK

#check_PROGRAMS			= mcast_test

# "*_stub.*" files aren't included here because all "_SOURCES" files are
# expected by "make distcheck" and they won't exist on a system that doesn't
# have Opmock.
#mcast_test_SOURCES	= mcast_test.cpp mcast.cpp
#mcast_test_CPPFLAGS	= $(test_cppflags)
#mcast_test_LDFLAGS	= @OPMOCK_LDFLAGS@
#
# The stubs are included here because they can't be in "_SOURCES" above.
#mcast_test_LDADD		= VCMTPReceiver_stub.o \
#				  $(top_builddir)/lib/libldm.la \
#				  @OPMOCK_LDADD@
endif

if HAVE_CUNIT
endif

check_PROGRAMS			= $(opmock_check_progs) $(cunit_check_progs)
TESTS				= $(opmock_tests) $(cunit_tests)
