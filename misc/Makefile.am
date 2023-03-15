# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

CLEANFILES		= $(BUILT_SOURCES) *.i *_test
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
	alrm.c \
	AtomicInt.c AtomicInt.h \
	AtomicFlag.c AtomicFlag.h \
	child_map.c child_map.h \
	ChildCmd.c ChildCmd.h \
        doubly_linked_list.c doubly_linked_list.h \
        doubly_linked_stack.c doubly_linked_stack.h \
	error.c \
	ErrObj.c ErrObj.h \
	fdnb.c \
        FixedDelayQueue.h \
	fsStats.c \
	InetId.c InetId.h \
	InetSockAddr.c InetSockAddr.h \
	inetutil.c \
	mkdirs_open.c \
	mygetline.c mygetline.h \
	pattern.c \
	queue.c queue.h \
	RegularExpressions.c \
	rpcutil.c \
	setenv.c \
	semRWLock.c semRWLock.h \
	statsMath.c \
	StrBuf.c StrBuf.h \
	StringBuf.c StringBuf.h \
	Thread.c Thread.h \
        StopFlag.c StopFlag.h
#        Future.c Future.h \
#        Executor.c Executor.h \
#        Completer.c Completer.h
AM_CPPFLAGS	= \
    -I$(top_builddir) -I$(top_srcdir) \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/log \
    -I$(top_srcdir)/mcast_lib/ldm7
TAGS_FILES	= \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../log/*.c ../log/*.h \
    ../rpc/*.c ../rpc/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h

.c.i:
	$(CPP) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(lib_la_CPPFLAGS) \
	    $(AM_CPPFLAGS) $(CPPFLAGS) $< >$@

check_PROGRAMS	=

if HAVE_CUNIT

#done_test_SOURCES		= done_test.c
#done_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
#done_test_LDADD			= $(top_builddir)/lib/libldm.la -lpthread \
#				  @LIBS_CUNIT@
#check_PROGRAMS    		+= done_test

#CunitAssertTest_SOURCES		= CunitAssertTest.c
#CunitAssertTest_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
#CunitAssertTest_LDADD   	= $(top_builddir)/lib/libldm.la \
#                                  @LIBS_CUNIT@

read_close_test_SOURCES		= read_close_test.c
read_close_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
read_close_test_LDADD		= -lpthread @LIBS_CUNIT@
check_PROGRAMS    		+= read_close_test

#Executor_test_SOURCES		= Executor_test.c
#Executor_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
#Executor_test_LDADD		= $(top_builddir)/lib/libldm.la -lpthread \
#                                  @LIBS_CUNIT@
#check_PROGRAMS    		+= Executor_test

#Completer_test_SOURCES		= Completer_test.c
#Completer_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
#Completer_test_LDADD		= $(top_builddir)/lib/libldm.la -lpthread \
#                                  @LIBS_CUNIT@
#check_PROGRAMS    		+= Completer_test

#Task_test_SOURCES		= Task_test.c
#Task_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
#Task_test_LDADD			= $(top_builddir)/lib/libldm.la -lpthread \
#                                  @LIBS_CUNIT@
#check_PROGRAMS			+= Task_test


test_inetutil_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_srcdir)/log \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@
test_inetutil_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@
check_PROGRAMS		+= test_inetutil

test_child_map_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/log \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@
test_child_map_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@
check_PROGRAMS		+= test_child_map

testSemRWLock_CPPFLAGS	= \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/log \
    -I$(top_srcdir) \
    @CPPFLAGS_CUNIT@
testSemRWLock_LDADD	= \
    $(top_builddir)/lib/libldm.la \
    @LIBS_CUNIT@
check_PROGRAMS		+= testSemRWLock

ChildCmd_test_SOURCES	= ChildCmd_test.c
ChildCmd_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
ChildCmd_test_LDADD	= $(top_builddir)/lib/libldm.la -lpthread \
                          @LIBS_CUNIT@
check_PROGRAMS    	+= ChildCmd_test

queue_test_SOURCES	= queue_test.c
queue_test_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@
check-queue:		queue_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./queue_test
check_PROGRAMS		+= queue_test

doubly_linked_list_test_SOURCES	= doubly_linked_list_test.c 
doubly_linked_list_test_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@
check-doubly_linked_list:	doubly_linked_list_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./doubly_linked_list_test
check_PROGRAMS		+= doubly_linked_list_test

#Task_test_SOURCES		= Task_test.c
#Task_test_CPPFLAGS		= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@
#Task_test_LDADD			= $(top_builddir)/lib/libldm.la -lpthread \
#                                  @LIBS_CUNIT@
#check_PROGRAMS			+= Task_test

#executor_test_SOURCES	= executor_test.c
# `executor.c` is in the list to maintain the dependency graph.
#executor_test_LDADD	=  executor.lo $(top_builddir)/lib/libldm.la \
#			   @LIBS_CUNIT@
#check-executor:		executor_test
#	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
#	    --leak-check=full --show-reachable=yes ./executor_test

TESTS			= $(check_PROGRAMS)

SUFFIXES:	.vg .sup .supp .log
                                          
%.log:	%
	./% >&$@

# Precursor to Valgrind suppression file
%.supp:	%
	test -e $<.sup && supOpt=--suppressions=$<.sup; \
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
	    --mode=execute valgrind --leak-check=full --show-reachable=yes \
	    $${supOpt} --gen-suppressions=all ./$< >&$@
# Valgrind output
%.vg:	%
	test -e $<.sup && supOpt=--suppressions=$<.sup; \
	PATH=$(top_builddir)/mldm_sender:$$PATH $(LIBTOOL) \
	    --mode=execute valgrind --leak-check=full --show-reachable=yes \
	    $${supOpt} ./$< >&$@

debug:		test_child_map
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb test_child_map

debug_inetutil:		test_inetutil
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb test_inetutil
endif
