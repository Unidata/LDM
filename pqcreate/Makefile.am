# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST 	= pqcreate.1.in
PQ_SUBDIR	= @PQ_SUBDIR@

bin_PROGRAMS	= pqcreate
AM_CPPFLAGS	= \
    -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/ldm7
pqcreate_LDADD	= $(top_builddir)/lib/libldm.la
TAGS_FILES	= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../log/*.c ../log/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h
nodist_man1_MANS	= pqcreate.1
CLEANFILES	= pqcreate.1 *.i

#pqcreate.1:	$(srcdir)/pqcreate.1.in
#	../regutil/substPaths <$? >$@.tmp
#	mv $@.tmp $@

.c.i:
	$(COMPILE) $(AM_CPPFLAGS) $(CPPFLAGS) -E $< >$@
