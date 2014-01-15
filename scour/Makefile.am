# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= scour.in scour.conf
dist_bin_SCRIPTS	= scour
dist_man1_MANS		= scour.1
DISTCLEANFILES		= scour

if MAINTAINER
scour.1:	$(srcdir)/scour.1.in
	$(top_srcdir)/registry/substPaths <$< >$@.tmp
	mv $@.tmp $@

scour.conf:	scour.conf.in
	$(top_srcdir)/registry/substPaths <$< >$@.tmp
	mv $@.tmp $@
endif

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/scour.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/scour.conf:
	$(INSTALL_DATA) $(srcdir)/scour.conf $@
