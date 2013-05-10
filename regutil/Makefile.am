# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Process this file with automake to produce Makefile.in

AWK			= @AWK@
PERL			= @PERL@
LD_GEN			= @LD_GEN@
LDMHOME			= @LDMHOME@
LOGS_DIR		= @LOGS_DIR@
QUEUE_DIR		= @QUEUE_DIR@
ETC_DIR			= $(sysconfdir)
DATA_DIR		= @DATA_DIR@
HOSTNAME		= @HOSTNAME@
NTPDATE			= @NTPDATE@
LOG_FILE		= $(LOGS_DIR)/ldmd.log
METRICS_FILE		= $(LOGS_DIR)/metrics.txt
NETSTAT			= @NETSTAT@
TOP			= @TOP@
bin_PROGRAMS		= regutil
EXTRA_DIST              = \
    checkRegistry \
    installRegistry \
    regpar.tab.in \
    regutil.txt \
    substPaths
TAGS_FILES              = \
    *.c \
    $(top_srcdir)/registry/*.c \
    $(top_srcdir)/registry/*.h \
    $(top_srcdir)/*.h \
    $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h \
    $(top_srcdir)/protocol/*.c \
    -lxml2 \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
AM_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir)/ulog \
    -I$(top_srcdir)/protocol \
    -lxml2
LDADD			= \
    $(top_builddir)/lib/libldm.la \
    -lxml2 \
    $(LD_GEN)

BUILT_SOURCES		= regpar.tab
dist_man1_MANS		= regutil.1
CLEANFILES		= regutil.out regpar.tab

# "regpar.tab" fields:
#
# id		Identifier in "regpath{id}" substitution expressions and 
# 		suffix of "REG_" C macro names
# path		Pathname of the parameter in the registry
# desc		Description of the parameter
# default	Default value
# perl-id	Identifier of corresponding perl(1) variable in ldmadmin(1)
# 		configuration-file, if it exists
regpar.tab:	$(srcdir)/regpar.tab.in
	sed -e 's;@''LDMHOME@;$(LDMHOME);g' \
	    -e 's;@''LOGS_DIR@;$(LOGS_DIR);g' \
	    -e 's;@''QUEUE_DIR@;$(QUEUE_DIR);g' \
	    -e 's;@''DATA_DIR@;$(DATA_DIR);g' \
	    -e 's;@''HOSTNAME@;$(HOSTNAME);g' \
	    -e 's;@''NTPDATE@;$(NTPDATE);g' \
	    -e 's;@''LOG_FILE@;$(LOG_FILE);g' \
	    -e 's;@''METRICS_FILE@;$(METRICS_FILE);g' \
	    -e 's;@''NETSTAT@;$(NETSTAT);g' \
	    -e 's;@''TOP@;$(TOP);g' \
	    -e 's;@''ETC_DIR@;$(ETC_DIR);g' \
	    $(srcdir)/regpar.tab.in >$@.tmp
	mv $@.tmp $@

if MAINTAINER
BUILT_SOURCES		+= substPaths

substPaths:	regpar.tab.in
	$(AWK) -F : 'BEGIN {print "sed \\"} \
	    {print "    -e '\''s:regpath{" $$1 "}:" $$2 ":g'\'' \\"} \
	    END {print ""}' regpar.tab.in >$@.tmp
	mv $@.tmp $@
	chmod +x $@

registry.h:	registry.hin registry.c regpar.tab.in
	$(AWK) '/@PARAMETER_NAME_DEFINITIONS@/{exit}{print}' \
	    $(srcdir)/registry.hin >$@.tmp1
	$(AWK) -F : '{print "#define REG_" $$1 " \"" $$2 "\""}' \
	    <regpar.tab.in >>$@.tmp1
	$(AWK) '{if (doPrint)print}/@PARAMETER_NAME_DEFINITIONS@/{doPrint=1}' \
	    $(srcdir)/registry.hin >>$@.tmp1
	$(top_srcdir)/extractDecls $@.tmp1 $(srcdir)/registry.c >$@.tmp2
	mv -f $@.tmp2 $@
	rm -f $@.tmp1 $@.sed

endif

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<

install-exec-local:	regpar.tab regutil $(DESTDIR)$(sysconfdir)
	$(PERL) $(srcdir)/installRegistry regpar.tab ./regutil \
	    $(DESTDIR)$(sysconfdir)/ldmadmin-pl.conf $(DESTDIR)$(sysconfdir)

$(DESTDIR)$(sysconfdir):
	mkdir $@

if HAVE_CUNIT
check_PROGRAMS		= testRegistry
testRegistry_CPPFLAGS	= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/registry \
    -I$(top_srcdir)/protocol \
    -I$(top_srcdir)/ulog \
    @CPPFLAGS_CUNIT@

testRegistry_LDADD		= \
    $(top_builddir)/lib/libldm.la \
    -lxml2 \
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
