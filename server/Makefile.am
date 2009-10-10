# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

LD_GEN			= @LD_GEN@
YACC			= @YACC@
EXTRA_DIST		= \
    abbr.h \
    acl.h \
    child_process_set.h \
    down6.h \
    DownHelp.h \
    feedTime.h \
    forn5_svc.h \
    ldmd.conf \
    parser.y \
    peer_info.h \
    priv.h \
    requester6.h \
    scanner.l \
    scanner.c \
    up6.h \
    UpFilter.h \
    wordexp.h
if WANT_FAUX_PQ
    PQ_DIR 		= fauxPq
else
    PQ_DIR 		= pq
endif
BUILT_SOURCES		= ldm6_svc.c parser.c scanner.c
bin_PROGRAMS		= rpc.ldmd
rpc_ldmd_SOURCES	= \
    abbr.c \
    acl.c \
    child_process_set.c \
    parser.c \
    down6.c \
    DownHelp.c \
    forn5_svc.c \
    ldm6_server.c \
    ldm6_svc.c \
    ldmd.c \
    priv.c \
    remote.c \
    requester6.c \
    svc5.c \
    up6.c \
    UpFilter.c \
    wordexp.c
CPPFLAGS		= -I$(top_srcdir)/ulog -I$(top_srcdir)/protocol -I$(top_srcdir)/pq -I$(top_srcdir)/misc -I$(top_srcdir)
LDADD			= \
    ../$(PQ_DIR)/lib.la \
    ../protocol/lib.la \
    ../ulog/lib.la \
    ../misc/lib.la \
    ../rpc/lib.la \
    $(LD_GEN) \
    -lm
dist_man1_MANS		= ldmd.1
TAGS_FILES		= \
    ../$(PQ_DIR)/*.c ../$(PQ_DIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

# Because both the client-side and server-side code that rpcgen(1) generates
# use identical function names like "hereis_6" and because the LDM links to
# both client-side and server-side functions, the server-side functions are
# renamed using a "_svc" suffix.
#
# Also, the target-file must be modified to be more portable.
#
ldm6_svc.c:	../protocol/ldm.x
	(echo '#include "ldmconfig.h"'; $(RPCGEN) -m ../protocol/ldm.x) | sed \
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
	(echo '#include "ldmconfig.h"'; $(RPCGEN) -Ss ../protocol/ldm.x) > $@

parser.c:	parser.y
	status=1; \
	$(YACC) parser.y && \
	    echo '#include "ldmconfig.h"' >parser.c && \
	    cat y.tab.c >>parser.c && \
	    status=0; \
	rm -f y.tab.c conf.tab.c; \
	test $$status -ne 0 && rm -f $@; \
	exit $$status

# NOTE: flex(1) is used instead of lex(1) in order to generate a scanner that
# can process "include" statements.
#
scanner.c:	scanner.l
	flex scanner.l
	mv lex.yy.c scanner.c

install-exec-hook:
	-cd $(mandir)/man1 && ln -s ldmd.1 rpc.ldmd.1

install-data-local:	$(sysconfdir)/ldmd.conf
$(sysconfdir)/ldmd.conf:
	cp ldmd.conf $@
