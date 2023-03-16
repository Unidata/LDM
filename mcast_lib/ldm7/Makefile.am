# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST              = \
			  sock_funcs.c \
			  send_test.c recv_test.c send_recv_test.c \
			  send_recv_test.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= \
			  down7_manager.c down7_manager.h \
			  up7.c up7.h \
			  prod_index_map.c prod_index_map.h \
			  mcast_info.c mcast_info.h \
			  MldmRcvr.c MldmRcvr.h \
			  MldmRcvrMemory.c MldmRcvrMemory.h \
			  UpMcastMgr.c UpMcastMgr.h \
			  mldm_sender_map.c mldm_sender_map.h \
			  prod_index_queue.c prod_index_queue.h \
			  VirtualCircuit.c VirtualCircuit.h \
			  CidrAddr.c CidrAddr.h \
			  fmtp.cpp fmtp.h \
			  ProdNotifier.cpp ProdNotifier.h \
			  SendNotifier.cpp SendNotifier.h \
			  Authorizer.cpp Authorizer.h \
			  TcpSock.cpp TcpSock.h \
			  Internet.cpp Internet.h \
			  FmtpClntAddrs.cpp FmtpClntAddrs.h \
			  MldmRpc.cpp MldmRpc.h
#			  AuthClient.cpp AuthClient.h
AM_CPPFLAGS		= -I$(builddir) -I$(srcdir) \
			  -I$(top_builddir) -I$(top_srcdir) \
			  -I$(top_builddir)/pq -I$(top_srcdir)/pq \
			  -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
			  -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
			  -I$(top_builddir)/registry -I$(top_srcdir)/registry \
			  -I$(top_builddir)/log -I$(top_srcdir)/log \
			  -I$(srcdir)/../FMTP-LDM7/UnidataFMTP/FMTPv3 \
			  -I$(srcdir)/../FMTP-LDM7/UnidataFMTP/FMTPv3/receiver \
			  -I$(srcdir)/../FMTP-LDM7/UnidataFMTP/FMTPv3/sender \
			  -I$(top_builddir)/misc -I$(top_srcdir)/misc \
			  -I$(top_srcdir)/rpc
dist_bin_SCRIPTS	= vlanUtil setMcastRate
# UpMcastMgr_test(1) calls mldm_sender(1), which calls
# pim_openForWriting(3), which creates *.map files in the product-queue
# directory
CLEANFILES		= *.yaml *_stub* *.map *.log *.pq *.conf *.pim *.vg *.supp \
			  *_test *.i
DISTCLEANFILES          = $(BUILT_SOURCES)

LDADD			=  $(top_builddir)/lib/libldm.la \
			   -L/usr/lib64/openssl11 -lssl -lcrypto

check_PROGRAMS		=
TESTS			=

publisher_SOURCES	= publisher.c
publisher_CPPFLAGS 	= -I/usr/include/openssl11
publisher_LDADD  	= -L/usr/lib64/openssl11 -lssl -lcrypto

subscriber_SOURCES	= subscriber.c
subscriber_CPPFLAGS 	= -I/usr/include/openssl11
subscriber_LDADD  	= -L/usr/lib64/openssl11 -lssl -lcrypto

check_PROGRAMS		+= publisher subscriber

send_test_SOURCES	= send_test.c send_recv_test.c send_recv_test.h
recv_test_SOURCES	= recv_test.c send_recv_test.c send_recv_test.h
check_PROGRAMS		+= send_test recv_test

server_test_SOURCES	= server_test.c
server_test_LDADD	= -lpthread
check_PROGRAMS		+= server_test
TESTS			+= server_test

if HAVE_OPMOCK

# "*_stub.*" files aren't included here because all "_SOURCES" files are
# expected by "make distcheck" and they won't exist on a system that doesn't
# have Opmock.
#mcast_test_SOURCES	= mcast_test.cpp fmtp.cpp
#mcast_test_CPPFLAGS	= $(test_cppflags)
#mcast_test_LDFLAGS	= @OPMOCK_LDFLAGS@
#
# The stubs are included here because they can't be in "_SOURCES" above.
#mcast_test_LDADD		= FMTPReceiver_stub.o \
#				  $(top_builddir)/lib/libldm.la \
#				  @OPMOCK_LDADD@

FMTPReceiver_stubs	= fmtpRecvv3_stub.cpp fmtpRecvv3_stub.hpp
mcast_stubs		= mcast_stub.c mcast_stub.h
pq_stubs		= pq_stub.c pq_stub.h
globals_stubs		= globals_stub.c globals_stub.h
mldm_sender_map_stubs	= mldm_sender_map_stub.c mldm_sender_map_stub.h
down7_stubs		= down7_stub.c down7_stub.h
all_stubs		= $(FMTPReceiver_stubs) \
			  $(mcast_stubs) \
			  $(pq_stubs) \
			  $(globals_stubs) \
			  $(mldm_sender_map_stubs) \
			  $(down7_stubs)
BUILT_SOURCES		+= $(all_stubs)
DISTCLEANFILES		+= $(all_stubs)
CLEANFILES		+= TEST-test.xml
OPMOCK_INCLUDES 	= -I/usr/lib/gcc/x86_64-redhat-linux/4.8.3/include \
			  -I/usr/include/c++/4.8.3 \
			  -I/usr/local/include \
			  -I/usr/include
$(FMTPReceiver_stubs):		../FMTP/FMTPv3/receiver/fmtpRecvv3.h
	opmock2 -cpp -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(mcast_stubs):			../C++/fmtp.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(pq_stubs):			$(top_srcdir)/pq/pq.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(globals_stubs):		$(top_srcdir)/registry/globals.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(mldm_sender_map_stubs):	mldm_sender_map.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(down7_stubs):			down7.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)

test_cppflags				= @OPMOCK_CPPFLAGS@

UpMcastMgr_test_SOURCES 	= UpMcastMgr_test.c
nodist_UpMcastMgr_test_SOURCES	= $(mcast_stubs)
UpMcastMgr_test_CPPFLAGS	= $(AM_CPPFLAGS) $(test_cppflags)
UpMcastMgr_test_LDFLAGS		= @OPMOCK_LDFLAGS@
UpMcastMgr_test_LDADD		= $(top_builddir)/lib/libldm.la \
				  	  @OPMOCK_LDADD@
check_PROGRAMS			+= UpMcastMgr_test
TESTS				+= UpMcastMgr_test

MldmRcvr_test_SOURCES 		= MldmRcvr_test.c MldmRcvr.c
nodist_MldmRcvr_test_SOURCES 	= $(mcast_stubs) $(pq_stubs) \
					  $(down7_stubs)
MldmRcvr_test_CPPFLAGS		= $(AM_CPPFLAGS) $(test_cppflags)
MldmRcvr_test_LDFLAGS		= @OPMOCK_LDFLAGS@
MldmRcvr_test_LDADD  		= $(top_builddir)/lib/libldm.la \
					  @OPMOCK_LDADD@
check_PROGRAMS			+= MldmRcvr_test
TESTS				+= MldmRcvr_test

MldmRcvrMemory_test_SOURCES 	= MldmRcvrMemory_test.c
nodist_MldmRcvrMemory_test_SOURCES = $(globals_stubs)
MldmRcvrMemory_test_CPPFLAGS	= $(AM_CPPFLAGS) $(test_cppflags)
MldmRcvrMemory_test_LDFLAGS	= @OPMOCK_LDFLAGS@
MldmRcvrMemory_test_LDADD  	= $(top_builddir)/lib/libldm.la \
					  @OPMOCK_LDADD@
check_PROGRAMS			+= MldmRcvrMemory_test
TESTS				+= MldmRcvrMemory_test
endif

if HAVE_GTEST
TcpSock_test_SOURCES	= TcpSock_test.cpp
TcpSock_test_LDADD	= $(LDADD) -lgtest
check_PROGRAMS		+= TcpSock_test
TESTS			+= TcpSock_test

Authorizer_test_SOURCES	= Authorizer_test.cpp
Authorizer_test_LDADD	= $(LDADD) -lgtest
check_PROGRAMS		+= Authorizer_test
#TESTS			+= Authorizer_test

#MldmAuth_test_SOURCES	= MldmAuth_test.cpp
#MldmAuth_test_LDADD  	= $(LDADD) -lgtest

endif

if HAVE_CUNIT

unsigned_test_SOURCES		= unsigned_test.c
unsigned_test_LDADD		= $(top_builddir)/lib/libldm.la -lpthread \
				  @LIBS_CUNIT@
check_PROGRAMS			+= unsigned_test
TESTS				+= unsigned_test

cond_wait_test_SOURCES		= cond_wait_test.c
cond_wait_test_LDADD		= $(top_builddir)/lib/libldm.la -lpthread \
				  @LIBS_CUNIT@
check_PROGRAMS			+= cond_wait_test
TESTS				+= cond_wait_test

mldm_sender_map_test_SOURCES	= mldm_sender_map_test.c mldm_sender_map.c
mldm_sender_map_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mldm_sender_map_test_LDADD	= $(top_builddir)/lib/libldm.la \
				  @LIBS_CUNIT@
check_PROGRAMS			+= mldm_sender_map_test
TESTS				+= mldm_sender_map_test

prod_index_map_test_SOURCES	= prod_index_map_test.c prod_index_map.c
prod_index_map_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
prod_index_map_test_LDADD	= $(top_builddir)/lib/libldm.la \
				  @LIBS_CUNIT@
check_PROGRAMS			+= prod_index_map_test
TESTS				+= prod_index_map_test

prod_index_queue_test_SOURCES	= prod_index_queue_test.c prod_index_queue.c
prod_index_queue_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
prod_index_queue_test_LDADD	= $(top_builddir)/lib/libldm.la \
				  @LIBS_CUNIT@
check_PROGRAMS			+= prod_index_queue_test
TESTS				+= prod_index_queue_test

MldmRpc_test_SOURCES		= MldmRpc_test.c
MldmRpc_test_LDADD		= $(LDADD) @LIBS_CUNIT@
check_PROGRAMS			+= MldmRpc_test
TESTS				+= MldmRpc_test

Down7_test_SOURCES		= Down7_test.c Up7Down7_lib.c Up7Down7_lib.h
Down7_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
Down7_test_LDADD		= $(top_builddir)/lib/libldm.la \
				  @LIBS_CUNIT@
check_PROGRAMS			+= Down7_test

Up7Down7_test_SOURCES		= Up7Down7_test.c Up7Down7_lib.c Up7Down7_lib.h
Up7Down7_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
Up7Down7_test_LDADD		= $(top_builddir)/lib/libldm.la \
				  @LIBS_CUNIT@
check_PROGRAMS			+= Up7Down7_test

AM_TESTS_ENVIRONMENT	= PATH=.:$(top_builddir)/mldm_sender:$$PATH;
# "Up7Down7_test" can't be part of "make check" because it depends on an
# *installed* mldm_sender(1).
TESTS				    += Up7Down7_test

# Conditional because Travis can't handle it
#installcheck-local: $(bindir)/mldm_sender Down7_test Up7Down7_test
#	pwd | grep travis >/dev/null || ./Up7Down7_test >&Up7Down7_test.log

SUFFIXES:	.i .c

.c.i:
	$(COMPILE) -E $< >$@

SUFFIXES:	.vg .sup .supp

# Precursor to Valgrind suppression file
%.supp:	%
	test -e $<.sup && supOpt=--suppressions=$<.sup; \
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
	    --mode=execute valgrind --leak-check=full --show-reachable=yes \
	    $${supOpt} --gen-suppressions=all ./$< >&$@
# Valgrind output
%.vg:	%
	test -e $<.sup && supOpt=--suppressions=$<.sup; \
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
	    --mode=execute valgrind --leak-check=full --show-reachable=yes \
	    $${supOpt} ./$< >&$@

endif
