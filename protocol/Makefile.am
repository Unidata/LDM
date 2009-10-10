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
    atofeedt.h \
    autoshift.h \
    globals.h \
    h_clnt.h \
    ldm.h \
    ldm5_clnt.h \
    ldmprint.h \
    ldm_clnt.h \
    ldm_xlen.h \
    md5.h \
    prod_class.h \
    savedInfo.h \
    timestamp.h \
    xdr_data.h
lib_la_SOURCES		= \
    atofeedt.c \
    autoshift.c \
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
    prod_info.c \
    prod_class.c \
    savedInfo.c \
    timestamp.c \
    xdr_data.c
lib_la_CPPFLAGS		= -I$(top_srcdir)/misc -I$(top_srcdir)/ulog -I$(top_srcdir)/pq -I$(top_srcdir)
#-I../server
TAGS_FILES		= \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h
EXTRA_DIST		= ldm.x ldm4.h ldm5.h rsaglobal.h prod_info.h

ldm.h:		ldm.x
	$(RPCGEN) -h ldm.x | \
	sed 's/typedef *char *signaturet/typedef unsigned char signaturet/' > $@

ldm_xdr.c:	ldm.x
	(echo '#include "ldmconfig.h"'; $(RPCGEN) -c ldm.x) | \
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
ldm6_clnt.c:	ldm.x
	(echo '#include "ldmconfig.h"'; $(RPCGEN) -l ldm.x) > $@.tmp || \
	    rm $@.tmp
	$(PERL) fix_clnt.pl < $@.tmp > $@ && rm $@.tmp       # fix stuff
