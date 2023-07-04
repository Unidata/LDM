# Copyright 2019 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    ldm7PreInstall \
    ldm7Install \
    ldmadmin.1.in \
    ldmadmin.pl.in \
    showSettings.in \
    ldmfail.in \
    netcheck.in \
    netcheck.conf \
    plotMetrics.in \
    syscheck.in
dist_bin_SCRIPTS	= \
    wasReceived \
    updateGempakTables \
    keep_running
nodist_bin_SCRIPTS	= \
    ldmadmin \
    showSettings \
    ldmfail \
    netcheck \
    plotMetrics \
    syscheck
nodist_man1_MANS	= ldmadmin.1
dist_man1_MANS	        = netcheck.1 syscheck.1 ldmfail.1 wasReceived.1
CLEANFILES              = ldmadmin ldmadmin.1 showSettings

ldmadmin:	ldmadmin.pl
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

ldmadmin.1:	$(srcdir)/ldmadmin.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/netcheck.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/netcheck.conf:
	$(INSTALL_DATA) $(srcdir)/netcheck.conf $@
