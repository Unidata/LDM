# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

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
    xdr_data.h
lib_la_SOURCES		= \
    abbr.c \
    acl.c \
    atofeedt.c \
    autoshift.c \
    down6.c \
    DownHelp.c \
    globals.c \
    h_clnt.c \
    ldm4_svc.c \
    ldm5_svc.c \
    ldm_xdr.c \
    ldm_xlen.c \
    ldm5_clnt.c \
    ldm6_clnt.c \
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
    timestamp.c \
    UpFilter.c \
    xdr_data.c
lib_la_CPPFLAGS		= -I$(top_srcdir)/misc -I$(top_srcdir)/ulog -I$(top_srcdir)/pq -I$(top_srcdir)
#-I../server
TAGS_FILES		= \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h
EXTRA_DIST		= \
    ldm.x \
    ldm4.h \
    ldm5.h \
    rsaglobal.h \
    prod_info.h \
    fix_clnt.pl
CLEANFILES		= *.i

ldm.h:		ldm.x
	$(RPCGEN) -h ldm.x | \
	sed 's/typedef *char *signaturet/typedef unsigned char signaturet/' > $@

ldm_xdr.c:	ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -c ldm.x) | \
	sed \
	  -e 's/xdr_opaque *( *xdrs, *objp, *16)/xdr_opaque(xdrs, (char*)objp, 16)/' \
	  -e '/#if defined(_LP64)/,/#endif/d' > $@

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
	$(CPP) $(CPPFLAGS) $(DEFS) $(DEFAULT_INCLUDES) $< >$@
