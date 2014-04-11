# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS			= vcmtp
EXTRA_DIST              = mcast_down_ldm.hin \
                          sock_funcs.c \
                          sock_funcs.hin
BUILT_SOURCES           = mcast_down_ldm.h \
                          sock_funcs.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= mcast_down_ldm.c \
			  PerFileNotifier.cpp PerFileNotifier.h \
                          vcmtp_c_api.cpp vcmtp_c_api.h
CPPFLAGS		= -I$(top_builddir) -I$(top_srcdir) \
                          -I$(top_builddir)/pq -I$(top_srcdir)/pq \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/ulog -I$(top_srcdir)/ulog \
                          -I$(builddir)/vcmtp/protocol -I$(srcdir)/vcmtp/protocol
lib_la_LIBADD		= vcmtp/CommUtil/lib.la \
                          vcmtp/protocol/lib.la
CLEANFILES		=
DISTCLEANFILES          = $(BUILT_SOURCES)

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
mcast_down_ldm.h:	mcast_down_ldm.hin mcast_down_ldm.c
sock_funcs.h:		sock_funcs.hin sock_funcs.c

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

#check_PROGRAMS			= vcmtp_c_api_test mcast_down_ldm_test
check_PROGRAMS			= mcast_down_ldm_test
test_cppflags			= $(CPPFLAGS) @OPMOCK_CPPFLAGS@

# "*_stub.*" files aren't included here because all "_SOURCES" files are
# expected by "make distcheck" and they won't exist on a system that doesn't
# have Opmock.
#vcmtp_c_api_test_SOURCES	= vcmtp_c_api_test.cpp vcmtp_c_api.cpp
#vcmtp_c_api_test_CPPFLAGS	= $(test_cppflags)
#vcmtp_c_api_test_LDFLAGS	= @OPMOCK_LDFLAGS@
mcast_down_ldm_test_SOURCES 	= mcast_down_ldm_test.c mcast_down_ldm.c
mcast_down_ldm_test_CPPFLAGS	= $(test_cppflags)
mcast_down_ldm_test_LDFLAGS	= @OPMOCK_LDFLAGS@
#
# The stubs are included here because they can't be in "_SOURCES" above.
#vcmtp_c_api_test_LDADD		= VCMTPReceiver_stub.o \
#				  $(top_builddir)/lib/libldm.la \
#				  @OPMOCK_LDADD@
mcast_down_ldm_test_LDADD	= vcmtp_c_api_stub.o \
				  pq_stub.o \
				  $(top_builddir)/lib/libldm.la \
				  @OPMOCK_LDADD@
TESTS				= $(check_PROGRAMS)
endif
