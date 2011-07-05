# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES	= lib.la
EXTRA_DIST              = \
    backend.h \
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
    $(top_srcdir)/libxml2/*.c \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
AM_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/libxml2/include

if MAINTAINER
backend.c:	backend-libxml2.c
	$(LN_S) backend-libxml2.c $@

registry.h:	registry.hin registry.c ../regutil/regpar.tab.in
	$(AWK) '/@PARAMETER_NAME_DEFINITIONS@/{exit}{print}' \
	    $(srcdir)/registry.hin >$@.tmp1
	$(AWK) -F : '{print "#define REG_" $$1 " \"" $$2 "\""}' \
	    <../regutil/regpar.tab.in >>$@.tmp1
	$(AWK) '{if (doPrint)print}/@PARAMETER_NAME_DEFINITIONS@/{doPrint=1}' \
	    $(srcdir)/registry.hin >>$@.tmp1
	$(top_srcdir)/extractDecls $@.tmp1 $(srcdir)/registry.c >$@.tmp2
	mv -f $@.tmp2 $@
	rm -f $@.tmp1 $@.sed

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
backend.h:	backend.hin backend.c
misc.h:		misc.hin misc.c
node.h:		node.hin node.c
stringBuf.h:	stringBuf.hin stringBuf.c
endif

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<
