# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

EXTRA_DIST		= registry.hin
BUILT_SOURCES           = registry.h
DISTCLEANFILES          = $(BUILT_SOURCES)
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES          = \
    backend.c backend.h \
    globals.c globals.h \
    misc.c misc.h \
    node.c node.h \
    registry.c \
    string_buf.c string_buf.h
TAGS_FILES              = \
    *.c *.h \
    ../*.h \
    ../log/*.c ../log/*.h \
    ../pq/*.c ../pq/*.h \
    ../protocol/*.c \
    ../protocol2/*.c \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
AM_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/log \
    -I$(top_srcdir)/pq \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/mcast_lib/ldm7

registry.h:	$(srcdir)/registry.hin $(srcdir)/../regutil/regpar.tab.in
	$(AWK) '/@PARAMETER_NAME_DEFINITIONS@/{exit}{print}' \
	    $(srcdir)/registry.hin >$@.tmp
	$(AWK) -F : '{print "#define REG_" $$1 " \"" $$2 "\""}' \
	    <$(srcdir)/../regutil/regpar.tab.in >>$@.tmp
	$(AWK) '{if (doPrint)print}/@PARAMETER_NAME_DEFINITIONS@/{doPrint=1}' \
	    $(srcdir)/registry.hin >>$@.tmp
	mv -f $@.tmp $@

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<

if HAVE_CUNIT
check_PROGRAMS		= testRegistry
testRegistry_CPPFLAGS	= \
    -I$(top_srcdir) \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/log \
    @CPPFLAGS_CUNIT@

testRegistry_LDADD	= $(top_builddir)/lib/libldm.la @LIBS_CUNIT@

TESTS			= testRegistry

debug:		testRegistry
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb testRegistry

valgrind:	testRegistry
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./testRegistry
endif
