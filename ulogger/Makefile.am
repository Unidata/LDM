# Copyright 2012 University Corporation for Atmospheric Research. All rights
# reserved.
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

bin_PROGRAMS		= ulogger
dist_bin_SCRIPTS	= newlog
CPPFLAGS		= -I$(top_srcdir)/log
LDADD			= $(top_builddir)/lib/libldm.la
TAGS_FILES              = ulogger.c ../log/log.c ../log/log.h \
                          ../log/log_private.h ../log/mutex.h
if WANT_ULOG
TAGS_FILES		+= ../log/log2ulog.c ../log/ulog/ulog.c \
			   ../log/ulog/ulog.h
else
TAGS_FILES		+= ../log/log2slog.c
endif
