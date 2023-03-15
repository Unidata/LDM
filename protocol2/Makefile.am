# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

YACC			= @YACC@
BUILT_SOURCES           = parser.c scanner.c
CLEANFILES		= *.i
EXTRA_DIST		= \
    child_process_set.h \
    exitStatus.h \
    forn5_svc.h \
    ldm4.h \
    ldm5.h \
    LdmConfFile.h \
    rsaglobal.h \
    parser.y \
    prod_info.h \
    scanner.c \
    scanner.l \
    up6.h \
    wordexp.h
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
    ldmfork.c ldmfork.h \
    ldmprint.c \
    ldm_clnt_misc.c \
    LdmConfFile.c \
    md5c.c \
    one_svc_run.c \
    parser.c \
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
    wordexp.c \
    xdr_data.c
lib_la_CPPFLAGS		= \
    -I$(top_builddir)          -I$(top_srcdir) \
    -I$(top_builddir)/misc     -I$(top_srcdir)/misc \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_builddir)/log      -I$(top_srcdir)/log \
    -I$(top_builddir)/pq       -I$(top_srcdir)/pq \
    -I$(top_builddir)/mcast_lib/ldm7 -I$(top_srcdir)/mcast_lib/ldm7
if WANT_MULTICAST
lib_la_CPPFLAGS		+= -I$(top_builddir)/mcast_lib/ldm \
                           -I$(top_srcdir)/mcast_lib/ldm
endif
TAGS_FILES		= \
    ../log/*.c ../log/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../misc/*.c ../misc/*.h \
    ../pq/*.c ../pq/*.h \
    ../ldmd/*.c \
    ../rpc/*.c ../rpc/*.h \
    ../registry/*.c
dist_man3_MANS		= uldb.3

parser.c:	parser.y
	status=1; \
	$(YACC) parser.y && \
	    mv y.tab.c parser.c && \
	    status=0; \
	rm -f y.tab.c conf.tab.c; \
	test $$status -ne 0 && rm -f $@; \
	exit $$status

# NOTE: flex(1) is used instead of lex(1) in order to generate a scanner that
# can process "include" statements.
#
scanner.c:	scanner.l parser.c
	flex scanner.l
	mv lex.yy.c scanner.c

# The following rule is for completeness only because the target-file must be
# manually modified to a great extent.
#
#ldm_server.c:	# ../protocol/ldm.x
#	(echo '#include "config.h"'; $(RPCGEN) -Ss ldm.x) > $@

.c.i:
	$(CPP) $(lib_la_CPPFLAGS) $(DEFS) $(DEFAULT_INCLUDES) $< >$@

local_checks		=

if HAVE_CUNIT

check_PROGRAMS		= timestamp_test ldmfork_test ldmprint_test \
			  test_data_prod testuldb

timestamp_test_SOURCES 	= timestamp_test.c timestamp.c
timestamp_test_CPPFLAGS	= -I$(top_srcdir) -I$(top_srcdir)/log @CPPFLAGS_CUNIT@
timestamp_test_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

ldmfork_test_CPPFLAGS	= -I$(top_srcdir) \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_srcdir)/log \
                          @CPPFLAGS_CUNIT@
ldmfork_test_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

ldmprint_test_CPPFLAGS  = -I$(top_srcdir) -I$(top_srcdir)/log \
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
                          -I$(top_srcdir)/log \
                          @CPPFLAGS_CUNIT@
test_data_prod_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

testuldb_CPPFLAGS	= -I$(top_srcdir) \
                          -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
                          -I$(top_builddir)/registry -I$(top_srcdir)/registry \
                          -I$(top_srcdir)/log \
                          @CPPFLAGS_CUNIT@
testuldb_LDADD		= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

TESTS			= timestamp_test ldmfork_test ldmprint_test \
                          test_data_prod testuldb

valgrind:	testuldb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --suppressions=testuldb.supp --leak-check=full \
	    --show-reachable=yes ./testuldb valgrind

endif
