# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

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

install-data-hook:		$(DESTDIR)$(mandir)/$(WHATIS)
$(DESTDIR)$(mandir)/$(WHATIS):	$(DESTDIR)$(mandir)
	@if test "$(MAKEWHATIS_CMD)"; then \
	    touch $@; \
	    eval $(MAKEWHATIS_CMD) || \
		echo 1>&2 "Couldn't build manual-page database"; \
	fi

install_setuids:
	chown root $(DESTDIR)$(bindir)/{hupsyslog,rpc.ldmd} \
	&& chmod 4755 $(DESTDIR)$(bindir)/{hupsyslog,rpc.ldmd} 

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
