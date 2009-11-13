# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

#SUBDIRS                = tokyocabinet
noinst_LTLIBRARIES      = lib.la
EXTRA_DIST              = rdblib.h
lib_la_SOURCES          = rdblib.c backend-bdb.c
lib_la_CPPFLAGS         = \
    -I$(top_srcdir) \
    -I$(top_srcdir)/ulog \
    -I$(srcdir)/tokyocabinet \
    -I/home/steve/Berkeley-DB/include
TAGS_FILES              = \
    *.c *.h \
    $(top_srcdir)/*.h \
    $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h \
    /home/steve/Berkeley-DB/db-4.8.24.NC/env/*.c \
    /home/steve/Berkeley-DB/db-4.8.24.NC/os/*.c \
    $(srcdir)/tokyocabinet/*.c $(srcdir)/tokyocabinet/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h

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
