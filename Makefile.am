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
PQ_SUBDIR	= @PQ_SUBDIR@

DISTCHECK_CONFIGURE_FLAGS	= --disable-root-actions

SUBDIRS	= \
    Berkeley-DB/build_unix \
    registry \
    rpc \
    ulog \
    misc \
    protocol \
    $(PQ_SUBDIR) \
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

DIST_SUBDIRS		= \
    Berkeley-DB/build_unix \
    registry \
    rpc \
    ulog \
    misc \
    protocol \
    pq \
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
    html \
    fauxPq \
    test

lib_LTLIBRARIES		= libldm.la
libldm_la_SOURCES	=
libldm_la_LIBADD	= \
    rpc/lib.la \
    ulog/lib.la \
    misc/lib.la \
    protocol/lib.la \
    $(PQ_SUBDIR)/lib.la

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
	&& ( test -e src || $(LN_S) runtime/src src ) \
	&& ( test -e bin || $(LN_S) runtime/bin bin ) \
	&& ( test -e doc || $(LN_S) runtime/doc doc ) \
	&& ( test -e include || $(LN_S) runtime/include include ) \
	&& ( test -e lib || $(LN_S) runtime/lib lib ) \
	&& ( test -e man || $(LN_S) runtime/man man ) \
	&& ( test -e runtime || $(LN_S) $(DESTDIR)$(prefix) runtime )

ensure-ldmhome-dirs:
	cd $(LDMHOME); \
	if test -e logs || mkdir logs; then \
	    if test -e queue; then \
		test -e data || mkdir data; \
	    else \
		if test -e data; then \
		    $(LN_S) data queue; \
		else \
		    mkdir data queue; \
		fi; \
	    fi; \
	fi

root-actions:	\
    install_setuids \
    ensure-syslog-entry \
    ensure-rpc-entry \
    ensure-services-entry

install_setuids:
	chown root $(DESTDIR)$(bindir)/hupsyslog \
	&& chown root $(DESTDIR)$(bindir)/ldmd \
	&& chmod 4755 $(DESTDIR)$(bindir)/hupsyslog \
	&& chmod 4755 $(DESTDIR)$(bindir)/ldmd
	@if ls -l $(DESTDIR)$(bindir)/ldmd | grep root >/dev/null \
	&& ls -l $(DESTDIR)$(bindir)/ldmd | grep rws >/dev/null \
	&& ls -l $(DESTDIR)$(bindir)/hupsyslog | grep root >/dev/null \
	&& ls -l $(DESTDIR)$(bindir)/hupsyslog | grep rws >/dev/null; then \
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
	if grep $(LOG_LDM) $(SYSLOG_CONF) >/dev/null; then \
	    : true; \
	else \
	    awk 'NF==2 && /\.none/{\
		print $$1 ";$(LOG_LDM).none	" $$2; next}{print}' \
		$(SYSLOG_CONF) >$(SYSLOG_CONF).new \
	    && echo >>$(SYSLOG_CONF).new \
	    && echo '# Unidata LDM:' >>$(SYSLOG_CONF).new \
	    && echo '$(LOG_LDM).debug	$(LDM_LOGFILE)' >>$(SYSLOG_CONF).new \
	    && cp $(SYSLOG_CONF) $(SYSLOG_CONF).old \
	    && mv -f $(SYSLOG_CONF).new $(SYSLOG_CONF); \
	fi

ensure-rpc-entry:
	if awk '$$2 == 300029{exit 1}' /etc/rpc; then \
	    cp -f /etc/rpc /etc/rpc.old; \
	    echo 'ldmd		300029	# Unidata LDM system' >>/etc/rpc; \
	fi

ensure-services-entry:
	if awk '$$2 ~ /388\/tcp/{exit 1}' /etc/services; then \
	    cp -f /etc/services /etc/services.old; \
	    echo 'ldm		388/tcp		# Unidata LDM' >>/etc/services;\
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
    ensure-rpc-entry \
    ensure-services-entry \
    ensure-ldmhome-links \
    ensure-ldmhome-dirs \
    commit-check \
    tag \
    ftp \
    beta
