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
lib_la_CPPFLAGS		= -I$(top_srcdir)/misc -I$(top_srcdir)/protocol2
lib_la_LIBADD		= $(builddir)/ulog/lib.la
MAIN_PAGE		= simple-logging.md
# The Doxygen documentation is part of the distribution so that it doesn't have
# to be created by the user.
dist_man3_MANS		= man/man3/log.h.3
EXTRA_DIST		= Doxyfile $(MAIN_PAGE) html $(dist_man3_MANS)
DOXYGEN			= @DOXYGEN@
docdir			= @DOCDIR_LIBRARIES@/simple-logging
CLEANFILES		= callgrind.out *.log

$(srcdir)/html \
$(srcdir)/$(dist_man3_MANS) \
$(srcdir)/html/index.html:	$(MAIN_PAGE) Doxyfile $(lib_la_sources)
	if test -z "$(DOXYGEN)"; then \
		echo "No doxygen(1) utility"; \
		exit 1; \
	fi
	cd $(srcdir) && $(DOXYGEN) Doxyfile

install-data-local:	$(srcdir)/html/index.html $(DESTDIR)$(docdir)
	cp -R $(srcdir)/html/* $(DESTDIR)$(docdir)

uninstall-local:
	chmod -R +w $(DESTDIR)$(docdir)
	rm -rf $(DESTDIR)$(docdir)

$(DESTDIR)$(docdir):
	mkdir -p $@

if WANT_SLOG
lib_la_SOURCES		+= log2slog.c
else
if WANT_ULOG
lib_la_SOURCES		+= log2ulog.c
lib_la_CPPFLAGS		+= -I$(srcdir)/ulog
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

callgrind:	log_test
	rm -f callgrind.out.*
	$(LIBTOOL) --mode=execute valgrind --tool=callgrind ./log_test
	kcachegrind
	rm callgrind.out.*
endif
