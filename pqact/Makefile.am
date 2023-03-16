# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    action.h \
    blackhole \
    filel.h \
    palt.h \
    pbuf.h \
    pqact.1.in \
    pqact.conf \
    pqact_test.conf \
    SharedCounter.h \
    state.h
GDBMLIB			= @GDBMLIB@
PQ_SUBDIR		= @PQ_SUBDIR@
bin_PROGRAMS		= pqact
check_PROGRAMS		= date_sub
pqact_SOURCES		= \
    action.c action.h \
    filel.c filel.h \
    palt.c palt.h \
    pbuf.c pbuf.h \
    pqact.c \
    state.c state.h
date_sub_SOURCES	= palt.c
AM_CPPFLAGS		= \
    -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/ldm7
date_sub_CPPFLAGS	= $(AM_CPPFLAGS) -UNDEBUG -DTEST_DATE_SUB
pqact_LDADD		= \
    $(top_builddir)/lib/libldm.la \
    $(GDBMLIB)
date_sub_LDADD		= $(top_builddir)/lib/libldm.la
nodist_man1_MANS	= pqact.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../log/*.c ../log/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
CLEANFILES              = pqact.1 *.i *.pq callgrind.out.* *.state

pqact.1:	$(srcdir)/pqact.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/pqact.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/pqact.conf:
	$(INSTALL_DATA) $(srcdir)/pqact.conf $@

.c.i:
	$(COMPILE) -E $< >$@

# pqact_test.conf is copied to /tmp to accomodate GitHub actions
check-local:	pqact
	rm -f /tmp/pqact_test.conf
	cp $(srcdir)/pqact_test.conf /tmp
	rm -f /tmp/pqact_test.conf.state
	../pqcreate/pqcreate -c -s 100k -S 100 -q pqact_test.pq
	./pqact -d $(srcdir) -q pqact_test.pq /tmp/pqact_test.conf & \
		pid=$$!; \
	../pqinsert/pq_test_insert -g 10000000 -q pqact_test.pq -m 2000 -n 1000; \
	sleep 1; \
	kill $$pid
	rm -f /tmp/pqact_test.conf.state pqact_test.pq

callgrind:	pqact
	rm -f pqact_test.conf.state callgrind.out.*
	../pqcreate/pqcreate -c -s 100k -S 100 -q pqact_test.pq
	$(LIBTOOL) --mode=execute valgrind --tool=callgrind \
	    ./pqact -d . -q pqact_test.pq ./pqact_test.conf & pid=$$!; \
	../pqinsert/pq_test_insert -q pqact_test.pq -m 2000 -n 1000; \
	sleep 5; \
	kill $$pid
	rm -f pqact_test.conf.state pqact_test.pq
	kcachegrind

pqact.vg:	pqact
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./pqact \
	    -o 200000000 -q /dev/null >&$@
