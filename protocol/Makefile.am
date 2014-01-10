# Copyright 2012 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

builtHeaders	        = \
    acl.h \
    data_prod.h \
    forn.h \
    globals.h \
    ldmfork.h \
    LdmProxy.h \
    timer.h \
    uldb.h
BUILT_SOURCES	        = ldm6_svc.c $(builtHeaders)
CLEANFILES		= *.i
DISTCLEANFILES		= $(builtHeaders)
EXTRA_DIST		= \
    $(builtHeaders:.h=.hin) \
    child_process_set.h \
    exitStatus.h \
    forn5_svc.h \
    ldm.x \
    ldm4.h \
    ldm5.h \
    rsaglobal.h \
    prod_info.h \
    fix_clnt.pl \
    up6.h
RPCGEN			= @RPCGEN@
PERL			= @PERL@
noinst_LTLIBRARIES	= lib.la
include_HEADERS		= \
    abbr.h \
    acl.h \
    atofeedt.h \
    autoshift.h \
    down6.h \
    DownHelp.h \
    feedTime.h \
    globals.h \
    h_clnt.h \
    ldm.h \
    ldm5_clnt.h \
    ldmprint.h \
    ldm_clnt.h \
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
    acl.c \
    atofeedt.c \
    autoshift.c \
    child_process_set.c \
    data_prod.c \
    down6.c \
    DownHelp.c \
    forn.c \
    forn5_svc.c \
    globals.c \
    h_clnt.c \
    LdmProxy.c \
    ldm4_svc.c \
    ldm5_svc.c \
    ldm6_svc.c \
    ldm_xdr.c \
    ldm_xlen.c \
    ldm5_clnt.c \
    ldm6_clnt.c \
    ldm6_server.c \
    ldmfork.c \
    ldmprint.c \
    ldm_clnt.c \
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
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/registry
TAGS_FILES		= \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../pq/*.c ../pq/*.h \
    ../ldmd/*.c \
    ../rpc/*.c ../rpc/*.h \
    ../registry/*.c
dist_man3_MANS		= uldb.3

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
acl.h:		acl.hin acl.c
data_prod.h:	data_prod.hin data_prod.c
forn.h:		forn.hin forn.c
globals.h:	globals.hin globals.c
ldmfork.h:	ldmfork.hin ldmfork.c
LdmProxy.h:	LdmProxy.hin LdmProxy.c
timer.h:	timer.hin timer.c
uldb.h:		uldb.hin uldb.c

ldm.h:		ldm.x
	$(RPCGEN) -h ldm.x | \
	sed 's/typedef *char *signaturet/typedef unsigned char signaturet/' > $@

ldm_xdr.c:	ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -c ldm.x) | \
	sed \
	  -e 's/xdr_opaque *( *xdrs, *objp, *16)/xdr_opaque(xdrs, (char*)objp, 16)/' \
	  -e '/#if defined(_LP64)/,/#endif/d' > $@

# Because both the client-side and server-side code that rpcgen(1) generates
# use identical function names like "hereis_6" and because the LDM links to
# both client-side and server-side functions, the server-side functions are
# renamed using a "_svc" suffix.
#
# Also, the target-file must be modified to be more portable.
#
ldm6_svc.c:	ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -m ldm.x) | sed \
	    -e 's;<rpc/svc_soc.h>;<rpc/rpc.h>;' \
	    -e 's;feedme_6\([^A-Za-z_]\);feedme_6_svc\1;' \
	    -e 's;notifyme_6\([^A-Za-z_]\);notifyme_6_svc\1;' \
	    -e 's;is_alive_6\([^A-Za-z_]\);is_alive_6_svc\1;' \
	    -e 's;hiya_6\([^A-Za-z_]\);hiya_6_svc\1;' \
	    -e 's;hereis_6\([^A-Za-z_]\);hereis_6_svc\1;' \
	    -e 's;notification_6\([^A-Za-z_]\);notification_6_svc\1;' \
	    -e 's;comingsoon_6\([^A-Za-z_]\);comingsoon_6_svc\1;' \
	    -e 's;blkdata_6\([^A-Za-z_]\);blkdata_6_svc\1;' \
	    -e '/<stropts\.h>/d;' | \
	case `uname` in \
	    Darwin)	sed '/rpcsvcdirty/d';; \
	    *)		cat;; \
	esac >$@

# The following rule is for completeness only because the target-file must be
# manually modified to a great extent.
#
ldm6_server.c:	# ../protocol/ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -Ss ../protocol/ldm.x) > $@

# The client-side code created by rpcgen(1) must be modified to ensure that
# certain functions use batched RPC by having: 
#   1) a NULL XDR function for the return value;
#   2) a zero timeout; and (just to make sure)
#   3) a NULL pointer for the return value.
# #3 isn't necessary according to our RPC 4.0 source code
# <file:///opt/rpcsrc_40/rpc/clnt_tcp.c> but SUN's ONC+ Developer's Guide
# <http://docs.sun.com/db?p=/doc/802-1997> shows this in its examples (of
# course, it also uses "xdr_void" rather than NULL for the return-value
# XDR-routine (sheesh!)).
#
ldm6_clnt.c:	ldm.x fix_clnt.pl
	(echo '#include "config.h"'; $(RPCGEN) -l ldm.x) > $@.tmp || \
	    rm $@.tmp
	$(PERL) fix_clnt.pl < $@.tmp > $@ && rm $@.tmp       # fix stuff

.c.i:
	$(CPP) $(lib_la_CPPFLAGS) $(DEFS) $(DEFAULT_INCLUDES) $< >$@
    
if HAVE_CUNIT

check_PROGRAMS		= test_data_prod testuldb
test_data_prod_CPPFLAGS	= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/registry \
    @CPPFLAGS_CUNIT@

test_data_prod_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@
    
testuldb_CPPFLAGS	= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/registry \
    @CPPFLAGS_CUNIT@

testuldb_LDADD		= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@

TESTS			= test_data_prod testuldb

valgrind:	testuldb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --suppressions=testuldb.supp --leak-check=full \
	    --show-reachable=yes ./testuldb valgrind

endif
