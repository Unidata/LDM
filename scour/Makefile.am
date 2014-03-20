# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= scour.conf.in scour.1.in
nodist_sysconf_DATA     = scour.conf
dist_bin_SCRIPTS	= scour
nodist_man1_MANS	= scour.1
CLEANFILES              = scour.1 scour.conf
DISTCLEANFILES		= scour

scour.1:	$(srcdir)/scour.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

scour.conf:	scour.conf.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@
