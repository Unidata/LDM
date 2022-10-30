# Copyright 2014 University Corporation for Atmospheric Research. All rights
# reserved. See the file COPYRIGHT in the top-level source-directory of the
# package for copying and redistribution conditions.
#
# This file is part of the LDM package.
#
## Process this file with automake to produce Makefile.in

lib_LTLIBRARIES		= libldm.la
libldm_la_SOURCES	= 
libldm_la_LIBADD	= \
    $(NOAAPORT_LIBS) \
    $(top_builddir)/misc/lib.la \
    $(top_builddir)/log/lib.la \
    $(top_builddir)/pqinglib/lib.la \
    $(top_builddir)/@PQ_SUBDIR@/lib.la \
    $(top_builddir)/protocol/lib.la \
    $(top_builddir)/protocol2/lib.la \
    $(top_builddir)/registry/lib.la \
    $(top_builddir)/rpc/lib.la
if WANT_NOAAPORT
    libldm_la_LIBADD	+= \
	$(top_builddir)/grib2/lib.la \
	$(top_builddir)/gempak/lib.la
if WANT_RETRANS
    libldm_la_LIBADD	+= $(top_builddir)/noaaport/retrans/lib.la
endif
else
if WANT_GRIBINSERT
    libldm_la_LIBADD	+= \
	$(top_builddir)/grib2/lib.la \
	$(top_builddir)/gempak/lib.la
endif
endif
if WANT_MULTICAST
    libldm_la_LIBADD	+= $(top_builddir)/mcast_lib/lib.la
    libldm_la_LIBADD	+= -lstdc++
endif
