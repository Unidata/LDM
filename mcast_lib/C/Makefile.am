# Copyright 2014 University Corporation for Atmospheric Research
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
			  mldm_sender_manager.c mldm_sender_manager.h \
			  mldm_sender_map.c mldm_sender_map.h \
			  prod_index_queue.c
AM_CPPFLAGS		= -I$(builddir) -I$(srcdir) \
			  -I$(top_builddir) -I$(top_srcdir) \
                          -I$(top_builddir)/pq -I$(top_srcdir)/pq \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
                          -I$(top_builddir)/registry -I$(top_srcdir)/registry \
                          -I$(top_builddir)/mylog -I$(top_srcdir)/mylog \
                          -I$(srcdir)/../vcmtp/VCMTPv3 \
                          -I$(srcdir)/../vcmtp/VCMTPv3/receiver \
                          -I$(srcdir)/../vcmtp/VCMTPv3/sender \
                          -I$(top_builddir)/misc -I$(top_srcdir)/misc \
                          -I$(top_srcdir)/rpc \
                          -I$(top_srcdir)/mcast_lib/C++
# mldm_sender_manager_test(1) calls mldm_sender(1), which calls
# pim_openForWriting(3), which creates *.map files in the product-queue
# directory
CLEANFILES		= *.yaml *_stub* *.map *.log *.pq *.conf
DISTCLEANFILES          = $(BUILT_SOURCES)

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

local_checks		=

if HAVE_OPMOCK
VCMTPReceiver_stubs	= vcmtpRecvv3_stub.cpp vcmtpRecvv3_stub.hpp
mcast_stubs		= mcast_stub.c mcast_stub.h
pq_stubs		= pq_stub.c pq_stub.h
globals_stubs		= globals_stub.c globals_stub.h
mldm_sender_map_stubs	= mldm_sender_map_stub.c mldm_sender_map_stub.h
down7_stubs		= down7_stub.c down7_stub.h
all_stubs		= $(VCMTPReceiver_stubs) \
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
$(VCMTPReceiver_stubs):		../vcmtp/VCMTPv3/receiver/vcmtpRecvv3.h
	opmock2 -cpp -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(mcast_stubs):			../C++/mcast.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(pq_stubs):			$(top_srcdir)/pq/pq.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(globals_stubs):		$(top_srcdir)/registry/globals.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(mldm_sender_map_stubs):	mldm_sender_map.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)
$(down7_stubs):			down7.h
	opmock2 -i $? -o . $(AM_CPPFLAGS) $(CPPFLAGS) $(OPMOCK_INCLUDES)

opmock_tests				= mldm_receiver_memory_test \
                                          mldm_receiver_test \
                                          mldm_sender_manager_test
opmock_check_progs			= $(opmock_tests)
test_cppflags				= @OPMOCK_CPPFLAGS@

mldm_sender_manager_test_SOURCES 	= mldm_sender_manager_test.c
nodist_mldm_sender_manager_test_SOURCES = $(mcast_stubs)
mldm_sender_manager_test_CPPFLAGS	= $(AM_CPPFLAGS) $(test_cppflags)
mldm_sender_manager_test_LDFLAGS	= @OPMOCK_LDFLAGS@
mldm_sender_manager_test_LDADD		= $(top_builddir)/lib/libldm.la \
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
                                          
check-mldm_sender_manager:	mldm_sender_manager_test
	./mldm_sender_manager_test
check-mldm_receiver:	mldm_receiver_test
	./mldm_receiver_test
endif

if HAVE_CUNIT
cunit_tests			= prod_index_map_test \
                                  mldm_sender_map_test \
                                  prod_index_queue_test \
                                  cond_wait_test
# `up7_down7_test is separate in order to have it built by `make check` but
# executed by `make check-local`.
cunit_check_progs		= $(cunit_tests) up7_down7_test
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
cond_wait_test_LDADD		= -lpthread @LIBS_CUNIT@

valgrind-prod_index_map_test:	prod_index_map_test
	$(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./prod_index_map_test
valgrind-prod_index_queue_test:	prod_index_queue_test
	$(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./prod_index_queue_test
valgrind-up7_down7_test:	up7_down7_test
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
            --mode=execute valgrind --leak-check=full --show-reachable=yes \
            --suppressions=up7_down7_test.supp \
            ./up7_down7_test
#            --suppressions=up7_down7_test.supp --gen-suppressions=all 

local_checks			+= check-up7_down7_test
check-up7_down7_test:		up7_down7_test
	PATH=$(top_builddir)/mldm_sender:$$PATH ./up7_down7_test
endif

check-local:			$(local_checks)
check_PROGRAMS			= $(opmock_check_progs) $(cunit_check_progs) \
				  server_test
TESTS				= $(opmock_tests) $(cunit_tests) server_test
