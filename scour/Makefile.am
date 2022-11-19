# Copyright 2021 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= scour.conf.in scour.1.in testCscour.py
BUILT_SOURCES		= scour.conf
nodist_bin_SCRIPTS	= scour.sh
bin_PROGRAMS		= scour
nodist_man1_MANS	= scour.1
CLEANFILES              = scour.1 scour.conf
AM_CPPFLAGS		= -I$(top_builddir)           -I$(top_srcdir) \
			  -I$(top_builddir)/log       -I$(top_srcdir)/log \
			  -I$(top_builddir)/registry  -I$(top_srcddir)/registry \
			  -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
			  -I$(top_builddir)/protocol  -I$(top_srcdir)/protocol \
			  -I$(top_builddir)/misc      -I$(top_srcdir)/misc 
scour_LDADD		= $(top_builddir)/lib/libldm.la 

scour.1:	$(srcdir)/scour.1.in
	$(builddir)/../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

scour.conf:	scour.conf.in
	$(builddir)/../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

install-data-local:    \
	$(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/scour.conf
$(DESTDIR)$(sysconfdir)/scour.conf:
	$(INSTALL_DATA) scour.conf $@

scour_SOURCES		= scour.c \
			  parser.c parser.h
TAGS_FILES		=  $(scour_SOURCES) ../log/*.c ../log/*.h 

TESTS_ENVIRONMENT	= LDM_REGISTRY_DIR=$(top_builddir)/regutil
#TESTS			= testCscour.py
