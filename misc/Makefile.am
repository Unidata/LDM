# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST	        = child_map.hin semRWLock.hin
BUILT_SOURCES		= ../registry/registry.h child_map.h semRWLock.h
CLEANFILES		    = child_map.h semRWLock.h
noinst_LTLIBRARIES	= lib.la
include_HEADERS		= \
	ldmalloc.h \
	alrm.h \
	error.h \
	fdnb.h \
	fsStats.h \
	inetutil.h \
	mkdirs_open.h \
	pattern.h \
	RegularExpressions.h \
	rpcutil.h \
	setenv.h \
	statsMath.h
lib_la_SOURCES	= \
	child_map.c child_map.h \
	error.c \
	fdnb.c \
	fsStats.c \
	inetutil.c \
	mkdirs_open.c \
	pattern.c \
	RegularExpressions.c \
	rpcutil.c \
	setenv.c \
    semRWLock.c semRWLock.h \
	statsMath.c \
	StringBuf.c StringBuf.h
lib_la_CPPFLAGS	= \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir)
TAGS_FILES	= \
    $(top_srcdir)/protocol/*.c $(top_srcdir)/protocol/*.h \
    $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h \
    $(top_srcdir)/registry/*.c $(top_srcdir)/registry/*.h \
    $(top_srcdir)/rpc/*.c $(top_srcdir)/rpc/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h

../registry/registry.h:
	cd ../registry && $(MAKE) $@

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
child_map.h:	child_map.hin child_map.c
semRWLock.h:	semRWLock.hin semRWLock.c

if HAVE_CUNIT
check_PROGRAMS		= test_child_map testSemRWLock
test_child_map_CPPFLAGS	= \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@

test_child_map_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    $(top_builddir)/libxml2/libxml2.la \
    @LIBS_CUNIT@

testSemRWLock_CPPFLAGS	= \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@

testSemRWLock_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    $(top_builddir)/libxml2/libxml2.la \
    @LIBS_CUNIT@

TESTS			= test_child_map testSemRWLock

debug:		test_child_map
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb test_child_map

valgrind:	test_child_map
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./test_child_map
endif
