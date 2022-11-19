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
    regutil.txt
TAGS_FILES              = \
    *.c ../*.h \
    ../log/*.c ../log/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    /usr/local/include/CUnit/CUnit.h \
    /usr/local/include/CUnit/Basic.h
AM_CPPFLAGS		= \
    -I$(top_srcdir) \
    -I$(top_srcdir)/registry \
    -I$(top_builddir)/registry \
    -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/mcast_lib/ldm7
regutil_LDADD		= $(top_builddir)/lib/libldm.la $(LD_GEN)

BUILT_SOURCES		= substPaths registry.xml
dist_man1_MANS		= regutil.1
CLEANFILES		= regutil.out substPaths registry.h registry.xml
DISTCLEANFILES		= regpar.tab

# "regpar.tab" fields:
#
# id        Identifier in "regpath{id}" substitution expressions and 
#           suffix of "REG_" C macro names
# path      Pathname of the parameter in the registry
# desc      Description of the parameter
# default   Default value
# perl-id   Identifier of corresponding perl(1) variable in ldmadmin(1)
#           configuration-file, if it exists

substPaths:	$(srcdir)/regpar.tab.in
	$(AWK) -F : 'BEGIN {print "sed \\"} \
	    {print "    -e '\''s:regpath{" $$1 "}:" $$2 ":g'\'' \\"} \
	    END {print ""}' $(srcdir)/regpar.tab.in >$@.tmp
	mv $@.tmp $@
	chmod +x $@

registry.h:	registry.hin registry.c $(srcdir)/regpar.tab.in
	$(AWK) '/@PARAMETER_NAME_DEFINITIONS@/{exit}{print}' \
	    $(srcdir)/registry.hin >$@.tmp1
	$(AWK) -F : '{print "#define REG_" $$1 " \"" $$2 "\""}' \
	    <$(srcdir)/regpar.tab.in >>$@.tmp1
	$(AWK) '{if (doPrint)print}/@PARAMETER_NAME_DEFINITIONS@/{doPrint=1}' \
	    $(srcdir)/registry.hin >>$@.tmp1
	$(top_srcdir)/extractDecls $@.tmp1 $(srcdir)/registry.c >$@.tmp2
	mv -f $@.tmp2 $@
	rm -f $@.tmp1 $@.sed

.c.i:
	$(COMPILE) $(lib_la_CPPFLAGS) -E -o $@ $<

# For "make check". Doesn't account for existing, installed registry
registry.xml:	regpar.tab regutil
	$(PERL) $(srcdir)/installRegistry regpar.tab './regutil -l-' \
	    $(DESTDIR)$(sysconfdir)/ldmadmin-pl.conf .

# For "make install". Accounts for existing, installed registry
install-exec-local:	regpar.tab regutil $(DESTDIR)$(sysconfdir)
	$(PERL) $(srcdir)/installRegistry regpar.tab './regutil -l-' \
	    $(DESTDIR)$(sysconfdir)/ldmadmin-pl.conf $(DESTDIR)$(sysconfdir)

$(DESTDIR)$(sysconfdir):
	mkdir -p $@

if HAVE_CUNIT
TESTS			= checkRegistry

debug-regutil:		regutil
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb regutil
endif
