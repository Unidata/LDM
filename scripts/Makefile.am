# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    ldmadmin.pl.in \
    ldmcheck.in \
    ldmfail.in \
    netcheck.in \
    netcheck.conf \
    syscheck.in
BUILT_SOURCES		= ldmadmin
dist_bin_SCRIPTS	= \
    netcheck \
    syscheck \
    ldmcheck \
    ldmadmin \
    ldmfail \
    plotMetrics
dist_man1_MANS		= ldmadmin.1 netcheck.1 syscheck.1 ldmfail.1
DISTCLEANFILES		= ldmadmin

ldmadmin:	ldmadmin.pl ../registry/regpar.tab Makefile
	awk -F : '{printf "s:@REG_%s@:%s:\n", $$1, $$2}' \
	    ../registry/regpar.tab >$@.sed
	sed -f $@.sed <ldmadmin.pl >$@.tmp
	mv -f $@.tmp $@
	rm $@.sed

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/netcheck.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/netcheck.conf:
	$(INSTALL_DATA) $(srcdir)/netcheck.conf $@
