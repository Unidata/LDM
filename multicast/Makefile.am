# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST              = downMcastLdm.hin
BUILT_SOURCES		= downMcastLdm.h
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= downMcastLdm.c
lib_la_CPPFLAGS		= -I$(top_srcdir) -I$(top_srcdir)/ulog

.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@
downMcastLdm.h:		downMcastLdm.hin downMcastLdm.c
