# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= mcast.cpp mcast.h \
			  PerProdNotifier.cpp PerProdNotifier.h \
			  PerProdSendingNotifier.cpp PerProdSendingNotifier.h \
			  FixedDelayQueue.h \
			  Authorizer.cpp Authorizer.h \
			  TcpSock.cpp TcpSock.h \
			  Internet.cpp Internet.h \
			  MldmRpc.cpp MldmRpc.h
#			  AuthClient.cpp AuthClient.h
AM_CPPFLAGS	        = -I$(top_builddir) -I$(top_srcdir) \
                          -I$(top_builddir)/pq -I$(top_srcdir)/pq \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
                          -I$(top_builddir)/registry -I$(top_srcdir)/registry \
                          -I$(top_builddir)/log -I$(top_srcdir)/log \
                          -I../C \
                          -I$(srcdir)/../FMTP-LDM7/UnidataFMTP/FMTPv3 \
                          -I$(srcdir)/../FMTP-LDM7/UnidataFMTP/FMTPv3/receiver \
                          -I$(srcdir)/../FMTP-LDM7/UnidataFMTP/FMTPv3/sender \
                          -I$(top_builddir)/misc -I$(top_srcdir)/misc \
                          -I$(top_srcdir)/rpc

LDADD			=  $(top_builddir)/lib/libldm.la

check_PROGRAMS			=

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
#mcast_test_LDADD		= FMTPReceiver_stub.o \
#				  $(top_builddir)/lib/libldm.la \
#				  @OPMOCK_LDADD@
endif

if HAVE_GTEST
TcpSock_test_SOURCES	= TcpSock_test.cpp
TcpSock_test_LDADD	= $(LDADD) -lgtest

Authorizer_test_SOURCES	= Authorizer_test.cpp
Authorizer_test_LDADD	= $(LDADD) -lgtest

#MldmAuth_test_SOURCES	= MldmAuth_test.cpp
#MldmAuth_test_LDADD  	= $(LDADD) -lgtest

check_PROGRAMS		+= TcpSock_test Authorizer_test
endif

if HAVE_CUNIT
InAddrMgr_test_SOURCES	= InAddrMgr_test.c InAddrMgr.cpp
InAddrMgr_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
InAddrMgr_test_LDADD	= $(LDADD) @LIBS_CUNIT@

MldmRpc_test_SOURCES	= MldmRpc_test.c
MldmRpc_test_LDADD	= $(LDADD) @LIBS_CUNIT@

check_PROGRAMS		+= InAddrMgr_test MldmRpc_test
endif

TESTS			= $(check_PROGRAMS)
