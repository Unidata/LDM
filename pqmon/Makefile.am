# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST 	= pqmon.1.in
CLEANFILES      = pqmon.1
PQ_SUBDIR	= @PQ_SUBDIR@

bin_PROGRAMS	= pqmon
CPPFLAGS	= \
    -I$(top_srcdir)/ulog \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
pqmon_LDADD	= $(top_builddir)/lib/libldm.la
nodist_man1_MANS	= pqmon.1
TAGS_FILES	= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../registry/*.c ../registry/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

pqmon.1:	$(srcdir)/pqmon.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

debug:		pqmon
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb pqmon

valgrind:	pqmon
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./pqmon -S
