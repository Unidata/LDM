# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

EXTRA_DIST              = \
    backend.hin \
    backend-libxml2.c \
    globals.hin \
    misc.hin \
    node.hin \
    registry.hin \
    stringBuf.hin
BUILT_SOURCES           = \
    backend.c \
    backend.h \
    globals.h \
    misc.h \
    node.h \
    registry.h \
    stringBuf.h
include_HEADERS		= globals.h
DISTCLEANFILES          = $(BUILT_SOURCES)
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES          = \
    globals.c \
    misc.c \
    node.c \
    registry.c \
    stringBuf.c
nodist_lib_la_SOURCES	= backend.c
TAGS_FILES              = \
    *.c *.h \
    ../*.h \
    ../ulog/*.c ../ulog/*.h \
    ../pq/*.c ../pq/*.h \
    ../protocol/*.c \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
AM_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/pq \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry

backend.c:	$(srcdir)/backend-libxml2.c
	$(LN_S) $(srcdir)/backend-libxml2.c $@

registry.h:	registry.hin registry.c $(srcdir)/../regutil/regpar.tab.in
	$(AWK) '/@PARAMETER_NAME_DEFINITIONS@/{exit}{print}' \
	    $(srcdir)/registry.hin >$@.tmp1
	$(AWK) -F : '{print "#define REG_" $$1 " \"" $$2 "\""}' \
	    <$(srcdir)/../regutil/regpar.tab.in >>$@.tmp1
	$(AWK) '{if (doPrint)print}/@PARAMETER_NAME_DEFINITIONS@/{doPrint=1}' \
	    $(srcdir)/registry.hin >>$@.tmp1
	$(top_srcdir)/extractDecls $@.tmp1 $(srcdir)/registry.c >$@.tmp2
	mv -f $@.tmp2 $@
	rm -f $@.tmp1 $@.sed

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
backend.h:	$(srcdir)/backend.hin backend.c
	$(top_srcdir)/extractDecls $(srcdir)/backend.hin backend.c >$@.tmp
	mv -f $@.tmp $@
globals.h:	$(srcdir)/globals.hin $(srcdir)/globals.c
misc.h:		$(srcdir)/misc.hin $(srcdir)/misc.c
node.h:		$(srcdir)/node.hin $(srcdir)/node.c
stringBuf.h:	$(srcdir)/stringBuf.hin $(srcdir)/stringBuf.c

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<
