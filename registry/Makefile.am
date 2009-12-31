# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES      = lib.la
EXTRA_DIST              = \
    backend-bdb.c \
    backend.hin \
    checkRegistry \
    extractDecls \
    misc.hin \
    node.hin \
    registry.hin \
    regutil.txt \
    stringBuf.hin
BUILT_SOURCES		= \
    backend.c backend.h \
    misc.h \
    node.h \
    registry.h \
    stringBuf.h
lib_la_SOURCES          = \
    backend.c \
    misc.c \
    node.c \
    registry.c \
    stringBuf.c
TAGS_FILES              = \
    *.c *.h \
    $(top_srcdir)/*.h \
    $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h \
    $(top_srcdir)/protocol/*.c \
    $(top_srcdir)/Berkeley-DB/env/*.c \
    $(top_srcdir)/Berkeley-DB/os/*.c \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_builddir)/Berkeley-DB/build_unix
bin_PROGRAMS		= regutil
LDADD			= \
    lib.la \
    $(top_builddir)/protocol/lib.la \
    $(top_builddir)/rpc/lib.la \
    $(top_builddir)/ulog/lib.la \
    $(top_builddir)/Berkeley-DB/build_unix/libdb-4.8.la
CLEANFILES		= \
    regutil.out \
    backend.h \
    misc.h \
    node.h \
    registry.h \
    stringBuf.h

backend.c:	backend-bdb.c
	$(LN_S) backend-bdb.c $@

.hin.h:
	$(srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
backend.h:	backend.hin backend.c
misc.h:		misc.hin misc.c
node.h:		node.hin node.c
registry.h:	registry.hin registry.c
stringBuf.h:	stringBuf.hin stringBuf.c

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<

if HAVE_CUNIT
check_PROGRAMS		= testRegistry
testRegistry_CPPFLAGS	= \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I$(top_builddir)/Berkeley-DB/build_unix \
    @CPPFLAGS_CUNIT@
testRegistry_LDADD		= \
    lib.la \
    $(top_builddir)/protocol/lib.la \
    $(top_builddir)/ulog/lib.la \
    $(top_builddir)/rpc/lib.la \
    $(top_builddir)/Berkeley-DB/build_unix/libdb-4.8.la \
    @LIBS_CUNIT@
TESTS			= checkRegistry

debug:		testRegistry
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb testRegistry

valgrind:	testRegistry
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./testRegistry
endif
