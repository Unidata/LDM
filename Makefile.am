# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

LDMHOME		= @LDMHOME@
LOG_LDM		= @LOG_LDM@
SYSLOG_CONF	= @SYSLOG_CONF@
LDM_LOGFILE	= @LDM_LOGFILE@
SUDO		= @SUDO@
SU		= @SU@

DISTCHECK_CONFIGURE_FLAGS	= --disable-include-root-actions
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

install-data-hook:	ensure-ldmhome-links ensure-ldmhome-dirs
if INCLUDE_ROOT_ACTIONS
	@echo
	@echo "\
This next step will perform those installation actions that must be \
executed by the superuser.  \
If this step doesn't work (for example, if you don't know \
root's password), then the superuser will have to manually execute \
the command \"$(MAKE) root-actions\". \
	| fmt
	@echo
if HAVE_SUDO
	$(SUDO) $(MAKE) root-actions
else
if HAVE_SU
	$(SU) -c '$(MAKE) root-actions'
endif
endif
else
	@echo
	@echo "\
NOTE: The command \"$(MAKE) root-actions\" will have to be executed by the \
superuser in order to complete the installation process." \
	| fmt
	@echo
endif

ensure-ldmhome-links:
	cd $(LDMHOME) \
	&& ( ls bin >/dev/null || ln -s runtime/bin bin ) \
	&& ( ls doc >/dev/null || ln -s runtime/doc doc ) \
	&& ( ls include >/dev/null || ln -s runtime/include include ) \
	&& ( ls lib >/dev/null || ln -s runtime/lib lib ) \
	&& ( ls man >/dev/null || ln -s runtime/man man ) \
	&& ( ls runtime >/dev/null || ln -s $(DESTDIR)$(prefix) runtime )

ensure-ldmhome-dirs:
	cd $(LDMHOME) \
	&& ( ls data >/dev/null || mkdir data ) \
	&& ( ls logs >/dev/null || mkdir logs )

root-actions:	install_setuids ensure-syslog-entry

install_setuids:
	chown root $(DESTDIR)$(bindir)/{hupsyslog,ldmd} \
	&& chmod 4755 $(DESTDIR)$(bindir)/{hupsyslog,ldmd} 
	@if ls -l $(DESTDIR)$(bindir)/ldmd | grep -q root \
	&& ls -l $(DESTDIR)$(bindir)/ldmd | grep -q rws \
	&& ls -l $(DESTDIR)$(bindir)/hupsyslog | grep -q root \
	&& ls -l $(DESTDIR)$(bindir)/hupsyslog | grep -q rws; then \
	    : true; \
	else \
	    echo; \
	    echo "\
ERROR: The LDM server program (bin/ldmd) or the hupsyslog(1) \
program (bin/hupsyslog) is not owned by \"root\" or does not have \
the setuid bit enabled.  The command \"make install_setuids\" will have \
to be executed manually by the superuser on a system that allows these \
actions." \
	    | fmt; \
	    echo; \
	    exit 1; \
	fi

ensure-syslog-entry:
	if grep -q $(LOG_LDM) $(SYSLOG_CONF); then \
	    : true; \
	else \
	    awk '/\.none/{print $$1 ";$(LOG_LDM).none	" $$2; next}{print}' \
		$(SYSLOG_CONF) >$(SYSLOG_CONF).new \
	    && echo >>$(SYSLOG_CONF).new \
	    && echo '# Unidata LDM:' >>$(SYSLOG_CONF).new \
	    && echo '$(LOG_LDM).debug	$(LDM_LOGFILE)' >>$(SYSLOG_CONF).new \
	    && cp $(SYSLOG_CONF) $(SYSLOG_CONF).old \
	    && mv -f $(SYSLOG_CONF).new $(SYSLOG_CONF); \
	fi

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

.PHONY:		\
    root-actions \
    install_setuids \
    ensure-syslog-entry \
    ensure-ldmhome-links \
    ensure-ldmhome-dirs \
    commit-check \
    tag \
    ftp \
    beta
