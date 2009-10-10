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

install-data-local:	$(sysconfdir)/scour.conf
$(sysconfdir)/scour.conf:
	cp scour.conf $@
