# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= scour.conf.in scour.1.in testCscour.py
BUILT_SOURCES		= scour.conf
dist_bin_SCRIPTS	= scour
bin_PROGRAMS		= Cscour
nodist_man1_MANS	= scour.1
CLEANFILES              = scour.1 scour.conf
AM_CPPFLAGS		= -I$(top_builddir)/log    -I$(top_srcdir)/log
Cscour_LDADD		= $(top_builddir)/lib/libldm.la 

scour.1:	$(srcdir)/scour.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

scour.conf:	scour.conf.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

install-data-local:    \
	$(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/scour.conf
$(DESTDIR)$(sysconfdir)/scour.conf:
	$(INSTALL_DATA) scour.conf $@

Cscour_SOURCES		= Cscour.c Cscour.h \
			  parser.c parser.h
TAGS_FILES		=  $(Cscour_SOURCES) ../log/*.c ../log/*.h 

TESTS			= testCscour.py