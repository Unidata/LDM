# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

DISTCHECK_CONFIGURE_FLAGS	= --disable-include-install-setuids
include_HEADERS			= ldmconfig.h

if WANT_FAUX_PQ
    PQ_DIR = fauxPq
else
    PQ_DIR = pq
endif

SUBDIRS	= \
    rpc \
    ulog \
    misc \
    protocol \
    $(PQ_DIR) \
    pqcreate \
    pqcheck \
    pqinsert \
    pqcat \
    pqexpire \
    pqmon \
    pqutil \
    pqsend \
    pqing \
    pqact \
    pqsurf \
    ldmsend \
    rtstats \
    feedme \
    notifyme \
    server \
    ldmping \
    scripts \
    scour \
    regex \
    html

lib_LTLIBRARIES		= libldm.la
libldm_la_SOURCES	=
libldm_la_LIBADD	= \
    rpc/lib.la \
    ulog/lib.la \
    misc/lib.la \
    protocol/lib.la \
    $(PQ_DIR)/lib.la

EXTRA_DIST = \
    ANNOUNCEMENT \
    CHANGE_LOG \
    COPYRIGHT \
    README \
    RELEASE_NOTES

distName = $(PACKAGE)-$(VERSION)
distArchive = $(distName).tar.gz
DISTCLEANFILES	= *.log $(distArchive)
distuninstallcheck_listfiles = find . -type f -print | grep -v './etc/'

all:

if INCLUDE_INSTALL_SETUIDS
install-exec-hook:
	@echo
	@echo "\
This next step will set the owner of the LDM server program and the \
hupsyslog(1) program to \"root\" and enable the setuid bit for those \
programs.  These actions are necessary for proper operation of the LDM \
system.  If this step doesn't work (for example, if you don't know \
root's password), then you or someone else will have to manually execute \
the command \"make install_setuids\" as the root user." \
	| fmt
	@echo
	su -c 'make install_setuids'
	@if ls -l $(DESTDIR)$(bindir)/ldmd | grep -q root \
	&& ls -l $(DESTDIR)$(bindir)/ldmd | grep -q rws \
	&& ls -l $(DESTDIR)$(bindir)/hupsyslog | grep -q root \
	&& ls -l $(DESTDIR)$(bindir)/hupsyslog | grep -q rws; then \
	    : true; \
	else \
	    echo; \
	    echo "\
The owner of the LDM server program (bin/ldmd) or the hupsyslog(1) \
program (bin/hupsyslog) is not \"root\" or either program does not have \
the setuid bit enabled.  The command \"make install_setuids\" will have \
to be executed manually by the root user." \
	    | fmt; \
	    echo; \
	    exit 1; \
	fi
endif

install_setuids:
	chown root $(DESTDIR)$(bindir)/{hupsyslog,ldmd} \
	&& chmod 4755 $(DESTDIR)$(bindir)/{hupsyslog,ldmd} 

install-data-hook:		$(DESTDIR)$(mandir)/$(WHATIS)
$(DESTDIR)$(mandir)/$(WHATIS):	$(DESTDIR)$(mandir)
	@if test "$(MAKEWHATIS_CMD)"; then \
	    touch $@; \
	    eval $(MAKEWHATIS_CMD) || \
		echo 1>&2 "Couldn't build manual-page database"; \
	fi

commit-check:
	if git status -a >/dev/null; then \
	    echo 1>&2 'Package needs "git commit -a"'; \
	    exit 1; \
	fi

tag:
	git tag -f v$(VERSION)

ftp:		commit-check tag $(PACKAGE)-$(VERSION).tar.gz
	filename=$(PACKAGE)-$(VERSION).tar.gz \
	dest=ftp:/home/ftp/pub/ldm/$$filename \
	&& scp $$filename $$dest \
	&& ssh ftp chmod u+rw,g+rw,o=r $$dest

beta:		commit-check tag $(PACKAGE)-$(VERSION).tar.gz
	filename=$(PACKAGE)-$(VERSION).tar.gz \
	dest=ftp:/home/ftp/pub/ldm/beta/$$filename \
	&& scp $$filename $$dest \
	&& ssh ftp chmod u+rw,g+rw,o=r $$dest

.PHONY:		install_setuids commit-check tag ftp beta
