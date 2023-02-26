# Copyright 2016 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES	= lib.la
include_HEADERS		= pq.h fbits.h lcm.h
lib_la_SOURCES		= pq.c lcm.c
dist_man3_MANS		= pq.3
AM_CPPFLAGS		= \
    -I$(top_srcdir)/rpc \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/ldm7
TAGS_FILES		= \
    ../misc/*.c ../misc/*.h \
    ../log/*.c ../log/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../rpc/*.c ../rpc/*.h
CLEANFILES		= *.pq *.out *.log callgrind.out.* vgcore.* core.*

if HAVE_CUNIT

check_PROGRAMS		= pq_test

fileLockedBySelf_SOURCES	= fileLockedBySelf.c
fileLockedBySelf_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
fileLockedBySelf_LDADD		= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@
check_PROGRAMS			+= fileLockedBySelf

pq_test_SOURCES		= pq_test.c pq.c
pq_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
pq_test_LDADD		= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@
check_PROGRAMS		+= pq_test

TESTS			= $(check_PROGRAMS)

valgrind:	pq_test
	rm -f pq_test.pq pq_test.out
	$(LIBTOOL) --mode=execute valgrind --leak-check=full \
                --show-reachable=yes --gen-suppressions=all ./pq_test
	rm pq_test.pq pq_test.out

callgrind:	pq_test
	-rm -f pq_test.pq pq_test.out callgrind.out.*
	$(LIBTOOL) --mode=execute valgrind --tool=callgrind ./pq_test
	-rm pq_test.pq pq_test.out
	kcachegrind

endif
