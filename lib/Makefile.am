# Copyright 2013 University Corporation for Atmospheric Research. All rights
# reserved. See the file COPYRIGHT in the top-level source-directory of the
# package for copying and redistribution conditions.
#
# This file is part of the LDM package.
#
## Process this file with automake to produce Makefile.in

lib_LTLIBRARIES		= libldm.la
libldm_la_SOURCES	= dummy.c
libldm_la_LIBADD	= \
    $(NOAAPORT_LIBS) \
    $(top_builddir)/misc/lib.la \
    $(top_builddir)/pqinglib/lib.la \
    $(top_builddir)/@PQ_SUBDIR@/lib.la \
    $(top_builddir)/protocol/lib.la \
    $(top_builddir)/registry/lib.la \
    $(top_builddir)/rpc/lib.la \
    $(top_builddir)/ulog/lib.la
if WANT_NOAAPORT
    libldm_la_LIBADD	+= \
	$(top_builddir)/grib2/lib.la \
	$(top_builddir)/gempak/lib.la
else
if WANT_GRIBINSERT
    libldm_la_LIBADD	+= \
	$(top_builddir)/grib2/lib.la \
	$(top_builddir)/gempak/lib.la
endif
endif
#libldm_la_LIBADD	+= $(LIBS)
