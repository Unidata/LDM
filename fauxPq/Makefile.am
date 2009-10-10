# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

XML_INC			= # needs to be set in order for this to work
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= fauxPq.c fauxPqConfigFile.c
lib_la_CPPFLAGS		= -I$(top_srcdir)/pq -I$(top_srcdir)/misc -I$(top_srcdir)/ulog -I$(top_srcdir)/protocol $(XML_INC)
EXTRA_DIST		= fauxPqConfigFile.h
data_DATA		= pq.xml
