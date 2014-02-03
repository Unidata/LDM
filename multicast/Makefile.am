# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS			= vcmtp
EXTRA_DIST              = mcast_down_ldm.hin
BUILT_SOURCES           = mcast_down_ldm.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= mcast_down_ldm.c
CPPFLAGS		= -I$(top_builddir) -I$(top_srcdir) \
                          -I$(top_builddir)/pq -I$(top_srcdir)/pq \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/ulog -I$(top_srcdir)/ulog \
                          -I$(builddir)/vcmtp/C_API -I$(srcdir)/vcmtp/C_API
lib_la_LIBADD		= vcmtp/C_API/lib.la \
                          vcmtp/CommUtil/lib.la \
                          vcmtp/protocol/lib.la
CLEANFILES		=
DISTCLEANFILES          = $(BUILT_SOURCES)

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
mcast_down_ldm.h:	mcast_down_ldm.hin mcast_down_ldm.c

if HAVE_OPMOCK
vcmtp_c_api_stubs	= vcmtp_c_api_stub.c vcmtp_c_api_stub.h
pq_stubs		= pq_stub.c pq_stub.h
all_stubs		= $(vcmtp_c_api_stubs) $(pq_stubs)
BUILT_SOURCES		+= $(all_stubs)
DISTCLEANFILES		+= $(all_stubs) TEST-test.xml
OPMOCK_INCLUDES 	= -I/usr/lib/gcc/x86_64-redhat-linux/4.8.2/include \
                          -I/usr/local/include \
                          -I/usr/include
$(vcmtp_c_api_stubs):	vcmtp/C_API/vcmtp_c_api.h
	opmock2 -i $? -o . $(OPMOCK_INCLUDES)
$(pq_stubs):		../pq/pq.h
	opmock2 -i $? -o . $(CPPFLAGS) $(OPMOCK_INCLUDES)

check_PROGRAMS			= mcast_down_ldm_test
#
# "pq_stub.*" and "vcmtp_c_api_stub.*" aren't included here because all
# "_SOURCES" files are expected by "make distcheck" and they won't exist on a
# system that doesn't have Opmock.
mcast_down_ldm_test_SOURCES 	= mcast_down_ldm_test.c mcast_down_ldm.c
mcast_down_ldm_test_CPPFLAGS	= $(CPPFLAGS) @OPMOCK_CPPFLAGS@
mcast_down_ldm_test_LDFLAGS	= @OPMOCK_LDFLAGS@
#
# The stubs are included here because they can't be in "_SOURCES" above.
mcast_down_ldm_test_LDADD	= vcmtp_c_api_stub.o \
				  pq_stub.o \
				  $(top_builddir)/lib/libldm.la \
				  @OPMOCK_LDADD@
TESTS				= $(check_PROGRAMS)
endif