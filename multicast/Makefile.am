# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS			= vcmtp
EXTRA_DIST              = down7.hin \
			  mcast_down.hin \
                          mcast_info.hin \
                          request_queue.hin \
                          server_info.hin \
                          sock_funcs.c sock_funcs.hin \
                          up_ldm.hin
BUILT_SOURCES           = down7.h \
			  mcast_down.h \
                          mcast_info.h \
                          request_queue.h \
                          server_info.h \
                          sock_funcs.h \
                          up_ldm.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= down7.c \
			  mcast_down.c \
			  PerFileNotifier.cpp PerFileNotifier.h \
			  LdmBofResponse.cpp LdmBofResponse.h \
			  mcast_info.c \
			  request_queue.c \
			  server_info.c \
			  up_ldm.c \
                          vcmtp_c_api.cpp vcmtp_c_api.h
CPPFLAGS		= -I$(top_builddir) -I$(top_srcdir) \
                          -I$(top_builddir)/pq -I$(top_srcdir)/pq \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/ulog -I$(top_srcdir)/ulog \
                          -I$(builddir)/vcmtp/protocol -I$(srcdir)/vcmtp/protocol \
                          -I$(top_srcdir)/misc -I$(top_srcdir)/misc \
                          -I$(top_srcdir)/rpc
lib_la_LIBADD		= vcmtp/libvcmtp.la -lpthread
CLEANFILES		=
DISTCLEANFILES          = $(BUILT_SOURCES)

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
down7.h:		down7.hin down7.c
mcast_down.h:		mcast_down.hin mcast_down.c
mcast_info.h:		mcast_info.hin mcast_info.c
request_queue.h:	request_queue.hin request_queue.c
server_info.h:		server_info.hin server_info.c
sock_funcs.h:		sock_funcs.hin sock_funcs.c
up_ldm.h:		up_ldm.hin up_ldm.c

if HAVE_OPMOCK
VCMTPReceiver_stubs	= VCMTPReceiver_stub.cpp VCMTPReceiver_stub.hpp
vcmtp_c_api_stubs	= vcmtp_c_api_stub.c vcmtp_c_api_stub.h
pq_stubs		= pq_stub.c pq_stub.h
all_stubs		= $(VCMTPReceiver_stubs) $(vcmtp_c_api_stubs) $(pq_stubs)
BUILT_SOURCES		+= $(all_stubs)
DISTCLEANFILES		+= $(all_stubs) TEST-test.xml
OPMOCK_INCLUDES 	= -I/usr/lib/gcc/x86_64-redhat-linux/4.8.2/include \
                          -I/usr/local/include \
                          -I/usr/include
$(VCMTPReceiver_stubs):	vcmtp/protocol/VCMTPReceiver.h
	opmock2 -cpp -i $? -o . $(OPMOCK_INCLUDES)
$(vcmtp_c_api_stubs):	vcmtp_c_api.h
	opmock2 -i $? -o . $(OPMOCK_INCLUDES)
$(pq_stubs):		../pq/pq.h
	opmock2 -i $? -o . $(CPPFLAGS) $(OPMOCK_INCLUDES)

#check_PROGRAMS			= vcmtp_c_api_test mcast_down
opmock_check_progs		= mcast_down_test
test_cppflags			= $(CPPFLAGS) @OPMOCK_CPPFLAGS@

# "*_stub.*" files aren't included here because all "_SOURCES" files are
# expected by "make distcheck" and they won't exist on a system that doesn't
# have Opmock.
#vcmtp_c_api_test_SOURCES	= vcmtp_c_api_test.cpp vcmtp_c_api.cpp
#vcmtp_c_api_test_CPPFLAGS	= $(test_cppflags)
#vcmtp_c_api_test_LDFLAGS	= @OPMOCK_LDFLAGS@
mcast_down_test_SOURCES 	= mcast_down_test.c mcast_down.c
mcast_down_test_CPPFLAGS	= $(test_cppflags)
mcast_down_test_LDFLAGS		= @OPMOCK_LDFLAGS@
#
# The stubs are included here because they can't be in "_SOURCES" above.
#vcmtp_c_api_test_LDADD		= VCMTPReceiver_stub.o \
#				  $(top_builddir)/lib/libldm.la \
#				  @OPMOCK_LDADD@
mcast_down_test_LDADD		= vcmtp_c_api_stub.o \
				  pq_stub.o \
				  $(top_builddir)/lib/libldm.la \
				  @OPMOCK_LDADD@
opmock_tests			= $(opmock_check_progs)
endif

if HAVE_CUNIT
cunit_check_progs		= test_request_queue
test_request_queue_SOURCES	= test_request_queue.c request_queue.c
test_request_queue_CPPFLAGS	= $(CPPFLAGS) @CPPFLAGS_CUNIT@
test_request_queue_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@
cunit_tests			= $(cunit_check_progs)

test_request_queue_valgrind:	test_request_queue
	$(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./test_request_queue
endif

check_PROGRAMS			= $(opmock_check_progs) $(cunit_check_progs)
TESTS				= $(opmock_tests) $(cunit_tests)