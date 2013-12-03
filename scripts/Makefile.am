# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    ldmadmin.in \
    ldmfail.in \
    netcheck.in \
    netcheck.conf \
    plotMetrics.in \
    syscheck.in
dist_bin_SCRIPTS	= \
    netcheck \
    syscheck \
    ldmadmin \
    ldmfail \
    wasReceived
nodist_bin_SCRIPTS	= plotMetrics
dist_man1_MANS		= ldmadmin.1 netcheck.1 syscheck.1 ldmfail.1 \
			  wasReceived.1
CLEANFILES		= ldmadmin

all:		ldmadmin

ldmadmin:	ldmadmin.pl
	$(top_srcdir)/registry/substPaths <$< >$@.tmp
	mv $@.tmp $@

ldmadmin.1:	ldmadmin.1.in
	$(top_srcdir)/registry/substPaths <$< >$@.tmp
	mv $@.tmp $@

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/netcheck.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/netcheck.conf:
	$(INSTALL_DATA) $(srcdir)/netcheck.conf $@
