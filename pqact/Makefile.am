# Copyright 2011 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    action.h \
    filel.h \
    palt.h \
    pbuf.h \
    pqact.1.in \
    pqact.conf \
    SharedCounter.h \
    state.h
GDBMLIB			= @GDBMLIB@
PQ_SUBDIR		= @PQ_SUBDIR@
bin_PROGRAMS		= pqact
check_PROGRAMS		= date_sub
pqact_SOURCES		= \
    action.c \
    filel.c \
    palt.c \
    pbuf.c \
    pqact.c \
    state.c
date_sub_SOURCES	= palt.c
CPPFLAGS		= \
    -I$(top_srcdir)/ulog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
date_sub_CPPFLAGS	= $(CPPFLAGS) -UNDEBUG -DTEST_DATE_SUB
pqact_LDADD		= \
    $(top_builddir)/lib/libldm.la \
    $(GDBMLIB)
date_sub_LDADD		= $(top_builddir)/lib/libldm.la
nodist_man1_MANS	= pqact.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../registry/*.c ../registry/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
CLEANFILES              = pqact.1 *.i 

pqact.1:	$(srcdir)/pqact.1.in
	../regutil/substPaths <$< >$@.tmp
	mv $@.tmp $@

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/pqact.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/pqact.conf:
	$(INSTALL_DATA) $(srcdir)/pqact.conf $@

.c.i:
	$(COMPILE) -E $< >$@

valgrind:	pqact
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./pqact -l /dev/null \
	    -o 200000000
