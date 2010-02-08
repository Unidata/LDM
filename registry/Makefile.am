# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

#nobase_localstate_FILES	= \
#    registry/__db.001 \
#    registry/__db.002 \
#    registry/__db.003 \
#    registry/__db.004 \
#    registry/registry.db
LDMHOME			= @LDMHOME@
REGISTRY_DIR		= $(DESTDIR)@REGISTRY_DIR@
noinst_LTLIBRARIES      = lib.la
EXTRA_DIST              = \
    backend-bdb.c \
    backend.hin \
    backend.h \
    checkRegistry \
    misc.hin \
    misc.h \
    node.hin \
    node.h \
    registry.hin \
    registry.h \
    regutil.txt \
    stringBuf.hin \
    stringBuf.h \
    configDefaults.txt
BUILT_SOURCES		= \
    backend.c backend.h \
    misc.h \
    node.h \
    registry.h \
    stringBuf.h \
    configDefaults.txt
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
AM_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -I$(top_builddir)/Berkeley-DB/build_unix \
    -DREGISTRY_PATH='"$(REGISTRY_DIR)"'
bin_PROGRAMS		= regutil
LDADD			= \
    lib.la \
    $(top_builddir)/protocol/lib.la \
    $(top_builddir)/rpc/lib.la \
    $(top_builddir)/ulog/lib.la \
    $(top_builddir)/Berkeley-DB/build_unix/libdb-4.8.la
dist_man1_MANS		= regutil.1
CLEANFILES		= regutil.out backend.c

backend.c:	backend-bdb.c
	$(LN_S) backend-bdb.c $@

registry.h:	registry.hin registry.c regpar.tab Makefile
	awk '/@PARAMETER_NAME_DEFINITIONS@/{exit}{print}' \
	    $(srcdir)/registry.hin >$@.tmp1
	awk -F : '{printf "#define REG_%s \"%s\"\n", $$1, $$2}' \
	    <$(srcdir)/regpar.tab >>$@.tmp1
	awk '{if (doPrint)print}/@PARAMETER_NAME_DEFINITIONS@/{doPrint=1}' \
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

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<

#registry:
#	mkdir $@
#registry/registry.db:
configDefaults.txt:	../config.h
	(echo '#include <config.h>'; \
	find $(top_srcdir) \( -path '*/registry' -o -path '*/Berkeley-DB' \) \
	    -prune -o -name \*.c -print \
	| xargs sed -n \
	    '/reg_getString(/{N;s/\n//;s/.*reg[A-Za-z_]*(//;s/).*//;p}' \
	| tr -s ', ' ' ' | awk '{print $$1, $$3}' ) >configDefaults.c
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o configDefaults.i configDefaults.c
	rm configDefaults.c
	sed -n '/^[^#]/s/"//gp' configDefaults.i >$@
	rm configDefaults.i

install-exec-local:	regpar.tab regutil
	-mkdir -p $(REGISTRY_DIR)
	./regutil -c
	while IFS=: read name path desc default; do \
	    ./regutil "$$path" >/dev/null 2>&1; \
	    status=$$?; \
	    if test $$status = 0; then \
		echo "Already in registry: \"$$path\" "; \
	    elif test $$status = 1; then \
		if test "$$name" = HOSTNAME; then \
		    ./regutil -s `uname -n` "$$path" || break; \
		else \
		    ./regutil -s "$$default" "$$path" || break; \
		fi; \
		echo "Added to registry: \"$$path\" "; \
	    else \
		echo 1>&2 "Error loading registry"; \
		exit 1; \
	    fi; \
	done < regpar.tab

uninstall-local:
	rm -f $(DESTDIR)$(mandir)/man1/regutil.1

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

debug-regutil:		regutil
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb regutil

valgrind:	testRegistry
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./testRegistry
endif
