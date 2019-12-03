# Copyright 2019 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS			= ulog
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= log.c log.h log_private.h
lib_la_CPPFLAGS		= -I$(top_srcdir)/misc -I$(top_srcdir)/protocol2
MAIN_PAGE		= simple-logging.md
# The Doxygen documentation is part of the distribution so that it doesn't have
# to be created by the user.
dist_man3_MANS		= man/man3/log.h.3
EXTRA_DIST		= log2slog.c log2ulog.c mutex.c mutex.h \
			  Doxyfile $(MAIN_PAGE) html $(dist_man3_MANS)
DOXYGEN			= @DOXYGEN@
docdir			= @DOCDIR_LIBRARIES@/simple-logging
CLEANFILES		= callgrind.out *.log *.vg *.supp

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

# The ulog(3) module is unconditionally included in the LDM library for use by
# third-party software (e.g., McIDAS)
lib_la_LIBADD		= $(builddir)/ulog/lib.la

if HAVE_CUNIT
check_PROGRAMS		= pthread_atfork_test log_test

pthread_atfork_test_SOURCES	= pthread_atfork_test.c

log_test_SOURCES	= log_test.c $(lib_la_SOURCES)
log_test_CPPFLAGS	= $(AM_CPPFLAGS) @CPPFLAGS_CUNIT@ $(lib_la_CPPFLAGS)
log_test_LDADD		= $(lib_la_LIBADD) $(top_builddir)/lib/libldm.la \
			  @LIBS_CUNIT@

TESTS			= $(check_PROGRAMS)

SUFFIXES:	.vg .sup .supp

# Precursor to Valgrind suppression file
%.supp:	%
	test -e $<.sup && supOpt=--suppressions=$<.sup; \
	$(LIBTOOL) --mode=execute valgrind --leak-check=full \
            --show-reachable=yes $${supOpt} --gen-suppressions=all ./$< >&$@

# Valgrind output
%.vg:	%
	test -e $<.sup && supOpt=--suppressions=$<.sup; \
	$(LIBTOOL) --mode=execute valgrind --leak-check=full \
            --show-reachable=yes $${supOpt} ./$< >&$@

valgrind:       log_test
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
		--leak-check=full --show-reachable=yes ./log_test

callgrind:	log_test
	rm -f callgrind.out.*
	$(LIBTOOL) --mode=execute valgrind --tool=callgrind ./log_test
	kcachegrind
	rm callgrind.out.*
endif
