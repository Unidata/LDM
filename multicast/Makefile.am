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
lib_la_CPPFLAGS		= -I$(top_srcdir) \
                          -I$(top_srcdir)/pq \
                          -I$(top_srcdir)/protocol \
                          -I$(top_srcdir)/ulog \
                          -Ivcmtp/c
lib_la_LIBADD		= vcmtp/c/lib.la \
                          vcmtp/c++/CommUtil/lib.la \
                          vcmtp/c++/protocol/lib.la
CLEANFILES		=
DISTCLEANFILES          = $(BUILT_SOURCES)

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
mcast_down_ldm.h:	mcast_down_ldm.hin mcast_down_ldm.c

if HAVE_OPMOCK
vcmtp_c_api_stubs	= vcmtp_c_api_stub.c vcmtp_c_api_stub.h
pq_stubs		= pq_stub.c pq_stub.h
CLEANFILES		+= $(vcmtp_c_api_stubs)
OPMOCK_INCLUDES 	= -I/usr/lib/gcc/x86_64-redhat-linux/4.8.2/include \
                          -I/usr/local/include \
                          -I/usr/include
$(vcmtp_c_api_stubs):	vcmtp/c/vcmtp_c_api.h
	opmock2 -i $(srcdir)/vcmtp/c/vcmtp_c_api.h -o . $(OPMOCK_INCLUDES)
$(pq_stubs):		../pq/pq.h
	opmock2 -i $(srcdir)/../pq/pq.h -o . $(lib_la_CPPFLAGS) \
		$(OPMOCK_INCLUDES)

check_PROGRAMS			= mcast_down_ldm_test
mcast_down_ldm_test_SOURCES 	= mcast_down_ldm_test.c \
				  mcast_down_ldm.c \
				  $(vcmtp_c_api_stubs) \
				  $(pq_stubs)
mcast_down_ldm_test_CPPFLAGS	= $(lib_la_CPPFLAGS) @OPMOCK_CPPFLAGS@
mcast_down_ldm_test_LDFLAGS	= @OPMOCK_LDFLAGS@
mcast_down_ldm_test_LDADD	= $(top_builddir)/lib/libldm.la @OPMOCK_LDADD@
TESTS				= mcast_down_ldm_test
endif