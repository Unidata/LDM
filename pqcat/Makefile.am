# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

if WANT_FAUX_PQ
    PQ_DIR	= fauxPq
else
    PQ_DIR	= pq
endif

bin_PROGRAMS	= pqcat
CPPFLAGS	= -I$(top_srcdir)/ulog -I$(top_srcdir)/protocol -I$(top_srcdir)/pq -I$(top_srcdir)/misc -I$(top_srcdir)
LDADD		= \
    ../$(PQ_DIR)/lib.la \
    ../protocol/lib.la \
    ../ulog/lib.la \
    ../misc/lib.la \
    ../rpc/lib.la \
    -lm
dist_man1_MANS	= pqcat.1
TAGS_FILES	= \
    ../$(PQ_DIR)/*.c ../$(PQ_DIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../ulog/*.c ../ulog/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h
