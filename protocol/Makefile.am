# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

BUILT_SOURCES		=  ldm_svc.c ldm.h ldm_clnt.c ldm_xdr.c
CLEANFILES		= *.i $(BUILT_SOURCES)
EXTRA_DIST		= ldm.x fix_clnt.pl
if WANT_MULTICAST
    RPCGEN_OPTS		= -DWANT_MULTICAST
endif
RPCGEN			= @RPCGEN@ -C $(RPCGEN_OPTS)
PERL			= @PERL@
noinst_LTLIBRARIES	= lib.la
nodist_include_HEADERS	= ldm.h
nodist_lib_la_SOURCES	= $(BUILT_SOURCES)
lib_la_CPPFLAGS		= -I$(top_srcdir) \
			  -I$(top_srcdir)/rpc \
			  -I$(top_srcdir)/log \
			  -I$(top_srcdir)/protocol2 \
			  -I$(top_srcdir)/misc \
			  -I$(top_builddir)/mcast_lib/ldm7 -I$(top_srcdir)/mcast_lib/ldm7
if WANT_MULTICAST
lib_la_CPPFLAGS		+= -I$(top_srcdir)/multicast
endif
TAGS_FILES		= ../rpc/*.c ../rpc/*.h

ldm.h:		ldm.x
	$(RPCGEN) -h $(srcdir)/ldm.x | \
	sed 's/typedef *char *signaturet/typedef unsigned char signaturet/' > $@

ldm_xdr.c:	ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -c $(srcdir)/ldm.x) | \
	sed \
	  -e 's;'$(srcdir)'/ldm\.h;ldm.h;' \
	  -e 's/xdr_opaque *( *xdrs, *objp, *16)/xdr_opaque(xdrs, (char*)objp, 16)/' \
	  -e '/#if defined(_LP64)/,/#endif/d' > $@

# Because both the client-side and server-side code that rpcgen(1) generates
# use identical function names like "hereis_6" and because the LDM links to
# both client-side and server-side functions, the server-side functions are
# renamed using a "_svc" suffix.
#
# Also, the target-file must be modified to be more portable.
#
ldm_svc.c:	ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -m $(srcdir)/ldm.x) | sed \
	    -e 's;'$(srcdir)'/ldm\.h;ldm.h;' \
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
# For LDM-7, some functions must use "message passing" (i.e., they must be sent
# immediately with no reply expected). This is done by having a zero timeout but
# an `xdr_void()` function for the (non-existant) return value.
#
ldm_clnt.c:	ldm.x fix_clnt.pl
	(echo '#include "config.h"'; $(RPCGEN) -l $(srcdir)/ldm.x) > $@.tmp || \
	    rm $@.tmp
	$(PERL) $(srcdir)/fix_clnt.pl < $@.tmp > $@ && rm $@.tmp # fix stuff

.c.i:
	$(CPP) $(lib_la_CPPFLAGS) $(DEFS) $(DEFAULT_INCLUDES) $< >$@
