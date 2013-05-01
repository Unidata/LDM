# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

BUILT_SOURCES   = parser.c scanner.c
CLEANFILES		= *.i .*.info
LD_GEN			= @LD_GEN@
YACC			= @YACC@
EXTRA_DIST		= \
    ldmd.1 \
    ldmd.conf \
    parser.y \
    scanner.c \
    scanner.l \
    wordexp.h
PQ_SUBDIR		= @PQ_SUBDIR@
bin_PROGRAMS	= ldmd
ldmd_SOURCES	= \
    parser.c \
    ldmd.c \
    wordexp.c
CPPFLAGS		= \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
LDADD			= \
    $(top_builddir)/lib/libldm.la \
    -lxml2 \
    $(LD_GEN) \
    -lm
dist_man1_MANS		= ldmd.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../registry/*.c ../registry/*.h \
    ../rpc/*.c ../rpc/*.h

if MAINTAINER
ldmd.1:		ldmd.1.in
	$(top_srcdir)/registry/substPaths <$< >$@.tmp
	mv $@.tmp $@
endif

if MAINTAINER

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
scanner.c:	scanner.l
	flex scanner.l
	mv lex.yy.c scanner.c

endif

SUFFIXES:	.h .hin
.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@

install-data-local:    \
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/ldmd.conf
$(DESTDIR)$(sysconfdir)/ldmd.conf:
	$(INSTALL_DATA) $(srcdir)/ldmd.conf $@

$(DESTDIR)$(sysconfdir):
	mkdir $@

uninstall-local:
	rm -f $(DESTDIR)$(mandir)/man1/ldmd.1

valgrind:	ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --suppressions=ldmd.supp --gen-suppressions=all --leak-check=full \
	    --show-reachable=yes --trace-children=no \
	    --child-silent-after-fork=no ./ldmd -l-

# The following doesn't work. If "--trace-children=no", then valgrind won't
# examine the LDM and if "--trace-children=yes", then valgrind won't execute
# the installed LDM because it's setuid and owned by root.
installcheck-local:
	valgrind --suppressions=ldmd.supp --gen-suppressions=all \
	    --leak-check=full --show-reachable=yes --trace-children=no \
	    --child-silent-after-fork=no ./valgrind-wrapper.sh \
	    $(DESTDIR)$(bindir)/ldmd -l-

debug:		ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb ./ldmd

.c.i:
	$(COMPILE) $(CPPFLAGS) -E -o $@ $<
    
if HAVE_CUNIT

debug_uldb:		testUpstreamLdmDb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb testUpstreamLdmDb

valgrind_uldb:	testUpstreamLdmDb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./testUpstreamLdmDb
        
endif
