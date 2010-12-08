# Copyright 2010 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

lib_LTLIBRARIES		= libldm.la
libldm_la_SOURCES	=
libldm_la_LIBADD	= \
    $(top_builddir)/misc/*.lo \
    $(top_builddir)/pqinglib/*.lo \
    $(top_builddir)/$(PQ_SUBDIR)/*.lo \
    $(top_builddir)/protocol/*.lo \
    $(top_builddir)/registry/*.lo \
    $(top_builddir)/rpc/*.lo \
    $(top_builddir)/ulog/*.lo
