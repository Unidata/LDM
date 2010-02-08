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
    child_process_set.h \
    forn5_svc.h \
    ldm6_svc.c \
    ldmd.conf \
    ldmd.conf.grow \
    parser.c \
    parser.y \
    scanner.c \
    scanner.l \
    up6.h \
    wordexp.h
PQ_SUBDIR		= @PQ_SUBDIR@
BUILT_SOURCES		= ldm6_svc.c parser.c scanner.c
bin_PROGRAMS		= ldmd
ldmd_SOURCES	= \
    child_process_set.c \
    parser.c \
    forn5_svc.c \
    ldm6_server.c \
    ldm6_svc.c \
    ldmd.c \
    svc5.c \
    up6.c \
    wordexp.c
CPPFLAGS		= \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
LDADD			= \
    ../$(PQ_SUBDIR)/lib.la \
    ../protocol/lib.la \
    ../ulog/lib.la \
    ../misc/lib.la \
    ../rpc/lib.la \
    $(LD_GEN) \
    ../registry/lib.la \
    ../Berkeley-DB/build_unix/libdb-4.8.la \
    -lm
dist_man1_MANS		= ldmd.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../registry/*.c ../registry/*.h \
    ../rpc/*.c ../rpc/*.h
CLEANFILES		= *.i
sysconf_DATA		= ldmd.conf.grow

# Because both the client-side and server-side code that rpcgen(1) generates
# use identical function names like "hereis_6" and because the LDM links to
# both client-side and server-side functions, the server-side functions are
# renamed using a "_svc" suffix.
#
# Also, the target-file must be modified to be more portable.
#
ldm6_svc.c:	../protocol/ldm.x
	(echo '#include "config.h"'; $(RPCGEN) -m ../protocol/ldm.x) | sed \
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

if MAINTAINER

parser.c:	parser.y
	status=1; \
	$(YACC) parser.y && \
	    echo '#include "config.h"' >parser.c && \
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

endif

install-data-local:    \
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/ldmd.conf
$(DESTDIR)$(sysconfdir)/ldmd.conf:
	$(INSTALL_DATA) $(srcdir)/ldmd.conf $@

uninstall-local:
	rm -f $(DESTDIR)$(mandir)/man1/ldmd.1

valgrind:	ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes \
	    --trace-children=no --child-silent-after-fork=yes ./ldmd -l-
#	    ./ldmd -l-

debug:		ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb ./ldmd

.c.i:
	$(COMPILE) $(CPPFLAGS) -E -o $@ $<
