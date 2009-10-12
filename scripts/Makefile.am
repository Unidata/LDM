# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= \
    ldmadmin.in \
    ldmcheck.in \
    ldmfail.in \
    netcheck.in \
    netcheck.conf \
    syscheck.in \
    writeConfiguration.pl.in
dist_bin_SCRIPTS	= \
    netcheck \
    syscheck \
    ldmcheck \
    ldmadmin \
    ldmfail \
    plotMetrics
dist_man1_MANS		= ldmadmin.1 netcheck.1 syscheck.1 ldmfail.1

install-exec-hook:
	confFile=$(DESTDIR)$(sysconfdir)/ldmadmin-pl.conf; \
	if [ -f "$$confFile" ]; then \
	    if cp $$confFile $$confFile.old; then \
		: true; \
	    else \
		echo 1>&2 "Couldn't copy old configuration-file"; \
		exit 1; \
	    fi; \
	    $(PERL) writeConfiguration.pl $$confFile >$$confFile.new; \
	else \
	    $(PERL) writeConfiguration.pl >$$confFile.new; \
	fi; \
	if test "$$?" -eq 0; then \
	    if mv -f $$confFile.new $$confFile; then \
		: true; \
	    else \
		echo 1>&2 "Couldn't rename new configuration-file"; \
		exit 1; \
	    fi; \
	else \
	    echo 1>&2 "Couldn't create new configuration-file"; \
	    exit 1; \
	fi

install-data-local:	\
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/netcheck.conf
$(DESTDIR)$(sysconfdir):
	$(MKDIR_P) $@
$(DESTDIR)$(sysconfdir)/netcheck.conf:
	$(INSTALL_DATA) $(srcdir)/netcheck.conf $@
