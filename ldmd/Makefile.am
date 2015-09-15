# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

BUILT_SOURCES           = parser.c scanner.c
CLEANFILES		= ldmd.1 *.i .*.info up7.conf down7.conf *.pq \
			  *.log *.pid
LD_GEN			= @LD_GEN@
YACC			= @YACC@
EXTRA_DIST		= ldmd.1.in \
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
ldmd_CPPFLAGS		= \
    -I$(top_srcdir)/mcast_lib \
    -I$(top_srcdir)/registry \
    -I$(top_builddir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/C \
    -I$(top_srcdir)/mcast_lib/C++
ldmd_LDADD		= \
    $(top_builddir)/lib/libldm.la \
    $(LD_GEN)
nodist_man1_MANS	= ldmd.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

ldmd.1:		$(srcdir)/ldmd.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

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

valgrind:	ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --suppressions=ldmd.supp --gen-suppressions=all --leak-check=full \
	    --show-reachable=yes --trace-children=no \
	    --child-silent-after-fork=no ./ldmd -l-

## The following doesn't work. If "--trace-children=no", then valgrind won't
## examine the LDM and if "--trace-children=yes", then valgrind won't execute
## the installed LDM because it's setuid and owned by root.
#installcheck-local:
#	valgrind --suppressions=ldmd.supp --gen-suppressions=all \
#	    --leak-check=full --show-reachable=yes --trace-children=no \
#	    --child-silent-after-fork=no ./valgrind-wrapper.sh \
#	    $(DESTDIR)$(bindir)/ldmd -l-

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

#if WANT_MULTICAST
check-local:
	$(top_builddir)/pqcreate/pqcreate -vl- -c -s 100M -q up7.pq
	$(top_builddir)/pqcreate/pqcreate -vl- -c -s 100M -q down7.pq
	echo 'ALLOW ANY .*' > up7.conf
	echo 'MULTICAST ANY 234.128.117.0:38800 1 127.0.0.1' >> up7.conf
	echo 'RECEIVE ANY localhost:38800' > down7.conf
	rm -f up7.log
	PATH=$(top_builddir)/mldm_sender:$$PATH ./ldmd -l up7.log -I 127.0.0.1 \
		-P 38800 -q up7.pq up7.conf >up7.pid
	sleep 1
	rm -f down7.log
	./ldmd -vl down7.log -q down7.pq down7.conf >down7.pid
	sleep 1
	rm -f pq_test_insert.log
	../pqinsert/pq_test_insert -vl pq_test_insert.log -q up7.pq -f NEXRAD3 \
		nexrad3.txt
	ps -e -o pid,ppid,pgid,args | grep ldm | grep -v grep
	sleep 1
	kill `cat down7.pid` `cat up7.pid`
	rm down7.pid up7.pid

#
#	&& sleep 1 \
#	&& kill $$up7_pid $$down7_pid
#endif
