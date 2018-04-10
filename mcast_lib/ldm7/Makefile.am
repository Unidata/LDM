# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST              = down7.hin \
			  mldm_receiver.hin \
			  mldm_receiver_memory.hin \
                          prod_index_queue.hin \
                          sock_funcs.c sock_funcs.hin \
                          up7.hin
BUILT_SOURCES           = down7.h \
			  mldm_receiver.h \
			  mldm_receiver_memory.h \
                          prod_index_queue.h \
                          sock_funcs.h \
                          up7.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= down7.c \
                          down7_manager.c down7_manager.h \
			  up7.c \
			  prod_index_map.c prod_index_map.h \
			  mcast_info.c mcast_info.h \
			  mldm_receiver.c \
			  mldm_receiver_memory.c \
			  UpMcastMgr.c UpMcastMgr.h \
			  mldm_sender_map.c mldm_sender_map.h \
			  prod_index_queue.c \
			  VirtualCircuit.c VirtualCircuit.h \
			  CidrAddr.c CidrAddr.h \
			  fmtp.cpp fmtp.h \
			  PerProdNotifier.cpp PerProdNotifier.h \
			  PerProdSendingNotifier.cpp PerProdSendingNotifier.h \
			  FixedDelayQueue.h \
			  Authorizer.cpp Authorizer.h \
			  TcpSock.cpp TcpSock.h \
			  Internet.cpp Internet.h \
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
# UpMcastMgr_test(1) calls mldm_sender(1), which calls
# pim_openForWriting(3), which creates *.map files in the product-queue
# directory
CLEANFILES		= *.yaml *_stub* *.map *.log *.pq *.conf *.pim *.vg
DISTCLEANFILES          = $(BUILT_SOURCES)

LDADD			=  $(top_builddir)/lib/libldm.la

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
down7.h:		down7.hin down7.c
mldm_receiver.h:	mldm_receiver.hin mldm_receiver.c
mldm_receiver_memory.h:	mldm_receiver_memory.c mldm_receiver_memory.hin
prod_index_queue.h:	prod_index_queue.hin prod_index_queue.c
sock_funcs.h:		sock_funcs.hin sock_funcs.c
up7.h:			up7.hin up7.c

server_test_SOURCES	= server_test.c
server_test_LDADD	= -lpthread
SERVER_TEST		= server_test

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

mldm_receiver_test_SOURCES 		= mldm_receiver_test.c mldm_receiver.c
nodist_mldm_receiver_test_SOURCES 	= $(mcast_stubs) $(pq_stubs) \
					  $(down7_stubs)
mldm_receiver_test_CPPFLAGS		= $(AM_CPPFLAGS) $(test_cppflags)
mldm_receiver_test_LDFLAGS		= @OPMOCK_LDFLAGS@
mldm_receiver_test_LDADD  		= $(top_builddir)/lib/libldm.la \
                                          @OPMOCK_LDADD@

mldm_receiver_memory_test_SOURCES 	= mldm_receiver_memory_test.c
nodist_mldm_receiver_memory_test_SOURCES = $(globals_stubs)
mldm_receiver_memory_test_CPPFLAGS	= $(AM_CPPFLAGS) $(test_cppflags)
mldm_receiver_memory_test_LDFLAGS	= @OPMOCK_LDFLAGS@
mldm_receiver_memory_test_LDADD  	= $(top_builddir)/lib/libldm.la \
                                          @OPMOCK_LDADD@
                                          
check-UpMcastMgr:	UpMcastMgr_test
	./UpMcastMgr_test
check-mldm_receiver:	mldm_receiver_test
	./mldm_receiver_test

MLDM_RECEIVER_MEMORY_TEST	= mldm_receiver_memory_test
MLDM_RECEIVER_TEST		= mldm_receiver_test
UP_MCAST_MGR_TEST		= UpMcastMgr_test
endif

if HAVE_GTEST
TcpSock_test_SOURCES	= TcpSock_test.cpp
TcpSock_test_LDADD	= $(LDADD) -lgtest

Authorizer_test_SOURCES	= Authorizer_test.cpp
Authorizer_test_LDADD	= $(LDADD) -lgtest

#MldmAuth_test_SOURCES	= MldmAuth_test.cpp
#MldmAuth_test_LDADD  	= $(LDADD) -lgtest

TCP_SOCK_TEST		= TcpSock_test
AUTHORIZER_TEST		= Authorizer_test
endif

if HAVE_CUNIT
# `up7_down7_test is separate in order to have it built by `make check` but
# executed by `make check-local`.
mldm_sender_map_test_SOURCES	= mldm_sender_map_test.c mldm_sender_map.c
mldm_sender_map_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
mldm_sender_map_test_LDADD	= $(top_builddir)/lib/libldm.la \
                                  @LIBS_CUNIT@
prod_index_map_test_SOURCES	= prod_index_map_test.c prod_index_map.c
prod_index_map_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
prod_index_map_test_LDADD	= $(top_builddir)/lib/libldm.la \
                                  @LIBS_CUNIT@
prod_index_queue_test_SOURCES	= prod_index_queue_test.c prod_index_queue.c
prod_index_queue_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
prod_index_queue_test_LDADD	= $(top_builddir)/lib/libldm.la \
                                  @LIBS_CUNIT@
up7_down7_test_SOURCES		= up7_down7_test.c
up7_down7_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
up7_down7_test_LDADD		= $(top_builddir)/lib/libldm.la \
                                  @LIBS_CUNIT@

cond_wait_test_SOURCES		= cond_wait_test.c
cond_wait_test_LDADD		= $(top_builddir)/lib/libldm.la -lpthread \
				  @LIBS_CUNIT@
				  
SUFFIXES:	.vg
%.vg:	%
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
            --mode=execute valgrind --leak-check=full --show-reachable=yes \
            ./$< >&$@

%.supp:	%
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
            --mode=execute valgrind --leak-check=full --show-reachable=yes \
            --gen-suppressions=all ./$< >&$@

valgrind-prod_index_map_test:	prod_index_map_test
	$(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./prod_index_map_test
valgrind-prod_index_queue_test:	prod_index_queue_test
	$(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./prod_index_queue_test
up7_down7_test.valgrind:	up7_down7_test
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
            --mode=execute valgrind --leak-check=full --show-reachable=yes \
            --suppressions=up7_down7_test.supp \
            ./up7_down7_test
#            --suppressions=up7_down7_test.supp --gen-suppressions=all 

check-up7_down7_test:		up7_down7_test
	PATH=$(top_builddir)/mldm_sender:$$PATH ./up7_down7_test
MldmRpc_test_SOURCES	= MldmRpc_test.c
MldmRpc_test_LDADD	= $(LDADD) @LIBS_CUNIT@

PROD_INDEX_MAP_TEST	= prod_index_map_test
MLDM_SENDER_MAP_TEST	= mldm_sender_map_test
PROD_INDEX_QUEUE_TEST	= prod_index_queue_test
COND_WAIT_TEST		= cond_wait_test
MLDM_RPC_TEST		= MldmRpc_test
UP7_DOWN7_TEST		= up7_down7_test
endif

TESTS_ENVIRONMENTS	= PATH=$(top_builddir)/mldm_sender:$$PATH
check_PROGRAMS    	= \
			$(COND_WAIT_TEST) \
			$(TCP_SOCK_TEST) \
			$(SERVER_TEST) \
			$(PROD_INDEX_MAP_TEST) \
			$(MLDM_SENDER_MAP_TEST) \
			$(PROD_INDEX_QUEUE_TEST) \
			$(AUTHORIZER_TEST) \
			$(MLDM_RECEIVER_MEMORY_TEST) \
                        $(MLDM_RECEIVER_TEST) \
                        $(UP_MCAST_MGR_TEST) \
			$(MLDM_RPC_TEST) \
			$(UP7_DOWN7_TEST)

check-local:	check-up7_down7_test
TESTS			= $(check_PROGRAMS)
