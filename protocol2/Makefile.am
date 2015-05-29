# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

hinHeaders	        = \
    data_prod.h \
    forn.h \
    ldm_config_file.h \
    ldmfork.h \
    LdmProxy.h \
    timer.h \
    uldb.h
BUILT_SOURCES	        =  $(hinHeaders)
CLEANFILES		= *.i $(hinHeaders)
EXTRA_DIST		= \
    $(hinHeaders:.h=.hin) \
    child_process_set.h \
    exitStatus.h \
    forn5_svc.h \
    ldm4.h \
    ldm5.h \
    rsaglobal.h \
    prod_info.h \
    up6.h
noinst_LTLIBRARIES	= lib.la
include_HEADERS		= \
    abbr.h \
    atofeedt.h \
    autoshift.h \
    down6.h \
    DownHelp.h \
    feedTime.h \
    h_clnt.h \
    ldm5_clnt.h \
    ldmprint.h \
    ldm_clnt_misc.h \
    ldm_xlen.h \
    md5.h \
    peer_info.h \
    priv.h \
    prod_class.h \
    remote.h \
    requester6.h \
    savedInfo.h \
    timestamp.h \
    UpFilter.h \
    uldb.h \
    xdr_data.h
lib_la_SOURCES		= \
    abbr.c \
    atofeedt.c \
    autoshift.c \
    child_process_set.c \
    data_prod.c \
    down6.c \
    DownHelp.c \
    forn.c \
    forn5_svc.c \
    h_clnt.c \
    LdmProxy.c \
    ldm4_svc.c \
    ldm5_svc.c \
    ldm_xlen.c \
    ldm5_clnt.c \
    ldm_server.c \
    ldmfork.c \
    ldmprint.c \
    ldm_clnt_misc.c \
    ldm_config_file.c \
    md5c.c \
    one_svc_run.c \
    priv.c \
    prod_info.c \
    prod_class.c \
    remote.c \
    requester6.c \
    savedInfo.c \
    svc5.c \
    timer.c \
    timestamp.c \
    up6.c \
    UpFilter.c \
    uldb.c \
    xdr_data.c
lib_la_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/misc \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/mcast_lib/C++
if WANT_MULTICAST
lib_la_CPPFLAGS		+= -I$(top_builddir)/mcast_lib -I$(top_srcdir)/mcast_lib
endif
TAGS_FILES		= \
    ../ulog/*.c ../ulog/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../misc/*.c ../misc/*.h \
    ../pq/*.c ../pq/*.h \
    ../ldmd/*.c \
    ../rpc/*.c ../rpc/*.h \
    ../registry/*.c
dist_man3_MANS		= uldb.3

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
data_prod.h:		data_prod.hin data_prod.c
forn.h:			forn.hin forn.c
ldm_config_file.h:	ldm_config_file.hin ldm_config_file.c
ldmfork.h:		ldmfork.hin ldmfork.c
LdmProxy.h:		LdmProxy.hin LdmProxy.c
timer.h:		timer.hin timer.c
uldb.h:			uldb.hin uldb.c

# The following rule is for completeness only because the target-file must be
# manually modified to a great extent.
#
#ldm_server.c:	# ../protocol/ldm.x
#	(echo '#include "config.h"'; $(RPCGEN) -Ss ldm.x) > $@

.c.i:
	$(CPP) $(lib_la_CPPFLAGS) $(DEFS) $(DEFAULT_INCLUDES) $< >$@

local_checks		=

if HAVE_CUNIT

check_PROGRAMS		= ldmprint_test test_data_prod testuldb

ldmprint_test_CPPFLAGS  = -I$(top_srcdir) -I$(top_srcdir)/ulog \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                           @CPPFLAGS_CUNIT@
ldmprint_test_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@
check-ldmprint_test:	ldmprint_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./ldmprint_test valgrind
local_checks		+= check-ldmprint_test

test_data_prod_CPPFLAGS	= -I$(top_srcdir) \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/registry -I$(top_srcdir)/registry \
                          -I$(top_srcdir)/ulog \
                          @CPPFLAGS_CUNIT@
test_data_prod_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

testuldb_CPPFLAGS	= -I$(top_srcdir) \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/registry -I$(top_srcdir)/registry \
                          -I$(top_srcdir)/ulog \
                          @CPPFLAGS_CUNIT@
testuldb_LDADD		= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

TESTS			= ldmprint_test test_data_prod testuldb

valgrind:	testuldb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --suppressions=testuldb.supp --leak-check=full \
	    --show-reachable=yes ./testuldb valgrind

endif
