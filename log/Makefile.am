# Copyright 2016 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS			= ulog
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= mutex.c mutex.h log.c log.h log_private.h
lib_la_LIBADD		= $(builddir)/ulog/lib.la
MAIN_PAGE		= main-page
EXTRA_DIST		= $(MAIN_PAGE) html
DOXYGEN			= @DOXYGEN@
CLEANFILES		= html

$(srcdir)/html \
$(srcdir)/html/index.html:	$(srcdir)/$(MAIN_PAGE) $(srcdir)/Doxyfile
	cd $(srcdir) && $(DOXYGEN) Doxyfile

if WANT_SLOG
lib_la_SOURCES		+= log2slog.c
else
if WANT_ULOG
lib_la_SOURCES		+= log2ulog.c
lib_la_CPPFLAGS		= -I$(srcdir)/ulog
endif
endif

if HAVE_CUNIT
check_PROGRAMS		= log_test
TESTS			= log_test

log_test_SOURCES	= log_test.c $(lib_la_SOURCES)
log_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@ $(lib_la_CPPFLAGS)
log_test_LDADD		= $(lib_la_LIBADD) $(top_builddir)/lib/libldm.la \
			  @LIBS_CUNIT@

valgrind:       log_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
		--leak-check=full --show-reachable=yes ./log_test
endif
