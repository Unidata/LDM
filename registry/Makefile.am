# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES      = lib.la
EXTRA_DIST              = registry.h backend.h stringBuf.h extractHeader
#lib_la_SOURCES          = registry.c backend-bdb.c stringBuf.c
lib_la_SOURCES          = stringBuf.c
lib_la_CPPFLAGS         = \
    -I$(top_srcdir) \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    -I/home/steve/Berkeley-DB/include
TAGS_FILES              = \
    *.c *.h \
    $(top_srcdir)/*.h \
    $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h \
    $(top_srcdir)/protocol/*.c \
    /home/steve/Berkeley-DB/db-4.8.24.NC/env/*.c \
    /home/steve/Berkeley-DB/db-4.8.24.NC/os/*.c \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
#bin_PROGRAMS		= rdbutil
#CPPFLAGS	= \
#    -I$(top_srcdir)/ulog \
#    -I$(top_srcdir)
#LDADD		= ../ulog/lib.la

.c.h:
	./extractHeader $*.h.in $< >$@.tmp
	mv -f $@.tmp $@
registry.h:	registry.h.in registry.c
stringBuf.h:	stringBuf.h.in stringBuf.c
backend.h:	backend.h.in backend.c
backend.c:	backend-bdb.c
	$(LN_S) backend-bdb.c $@
#registry.h:	registry.h.in registry.c
#	./extractHeader registry.h.in registry.c >$@.tmp
#	mv -f $@.tmp $@
#stringBuf.h:	stringBuf.h.in stringBuf.c
#	./extractHeader stringBuf.h.in stringBuf.c >$@.tmp
#	mv -f $@.tmp $@
#backend.h:	backend.h.in backend.c
#	./extractHeader backend.h.in backend.c >$@.tmp
#	mv -f $@.tmp $@

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<

if HAVE_CUNIT
check_PROGRAMS		= testRdb
testRdb_CPPFLAGS	= \
    -I$(top_srcdir)/ulog \
    -I/home/steve/Berkeley-DB/include \
    @CPPFLAGS_CUNIT@
testRdb_LDADD		= \
    lib.la \
    $(top_builddir)/ulog/lib.la \
    -L/home/steve/Berkeley-DB/lib -ldb \
    @LIBS_CUNIT@
TESTS			= testRdb

debug:		testRdb
	$(LIBTOOL) --mode=execute gdb testRdb

valgrind:	testRdb
	$(LIBTOOL) --mode=execute valgrind --leak-check=full \
	    --show-reachable=yes ./testRdb
endif
