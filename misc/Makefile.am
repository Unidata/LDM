# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

BUILT_SOURCES		= \
        child_map.h \
        doubly_linked_list.h \
        doubly_linked_stack.h \
        executor.h \
        semRWLock.h \
        StrBuf.h
EXTRA_DIST	        = \
        child_map.hin \
        doubly_linked_list.hin \
        doubly_linked_stack.hin \
        executor.hin \
        semRWLock.hin \
        StrBuf.hin
CLEANFILES		= $(BUILT_SOURCES) *.i
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
        doubly_linked_list.c doubly_linked_list.h \
        doubly_linked_stack.c doubly_linked_stack.h \
	error.c \
        executor.c executor.h \
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
	StrBuf.c \
	StringBuf.c StringBuf.h
lib_la_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/rpc \
    -I$(top_srcdir)
TAGS_FILES	= \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../rpc/*.c ../rpc/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h

.c.i:
	$(CPP) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(lib_la_CPPFLAGS) \
	    $(AM_CPPFLAGS) $(CPPFLAGS) $< >$@

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
child_map.h:		child_map.hin child_map.c
doubly_linked_list.h:	doubly_linked_list.hin doubly_linked_list.c
doubly_linked_stack.h:	doubly_linked_stack.hin doubly_linked_stack.c
executor.h:		executor.hin executor.c
semRWLock.h:		semRWLock.hin semRWLock.c
StrBuf.h:		StrBuf.hin StrBuf.c

if HAVE_CUNIT
check_PROGRAMS		= test_inetutil test_child_map testSemRWLock

test_inetutil_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@

test_inetutil_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@

test_child_map_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@

test_child_map_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@

testSemRWLock_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@

testSemRWLock_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@

TESTS			= test_inetutil test_child_map testSemRWLock

debug:		test_child_map
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb test_child_map

debug_inetutil:		test_inetutil
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb test_inetutil

valgrind:	test_child_map
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./test_child_map
endif
