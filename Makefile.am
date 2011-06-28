# Copyright 2011 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

LDMHOME		= @LDMHOME@
LOGS_DIR	= @LOGS_DIR@
QUEUE_DIR	= @QUEUE_DIR@
LOG_LDM		= @LOG_LDM@
SYSLOG_CONF	= @SYSLOG_CONF@
LDM_LOGFILE	= @LDM_LOGFILE@
SUDO		= @SUDO@
SU		= @SU@
PQ_SUBDIR	= @PQ_SUBDIR@

DISTCHECK_CONFIGURE_FLAGS	= --disable-root-actions

COMMON_LIB_SUBDIRS	= \
    libxml2 \
    rpc \
    ulog \
    misc \
    pqinglib \
    protocol \
    registry \
    lib

LIB_SUBDIRS	= \
    $(PQ_SUBDIR) \
    $(COMMON_LIB_SUBDIRS)

PROG_SUBDIRS	= \
    feedme \
    hupsyslog \
    ldmd \
    ldmping \
    ldmsend \
    notifyme \
    pqact \
    pqcat \
    pqcheck \
    pqcopy \
    pqcreate \
    pqexpire \
    pqing \
    pqinsert \
    pqmon \
    pqsend \
    pqsurf \
    pqutil \
    regex \
    regutil \
    rtstats \
    scour \
    scripts \
    ulogger

SUBDIRS		= \
    $(LIB_SUBDIRS) \
    $(PROG_SUBDIRS) \
    html

DIST_SUBDIRS	= \
    $(COMMON_LIB_SUBDIRS) \
    pq \
    fauxPq \
    $(PROG_SUBDIRS) \
    test html

nobase_include_HEADERS		= \
    rpc/rpc.h \
    rpc/types.h \
    rpc/xdr.h \
    rpc/auth.h \
    rpc/clnt.h \
    rpc/rpc_msg.h \
    rpc/auth_unix.h \
    rpc/svc.h \
    rpc/svc_auth.h \
    rpc/pmap_clnt.h \
    rpc/pmap_prot.h

EXTRA_DIST = \
    CHANGE_LOG \
    COPYRIGHT \
    extractDecls \
    ldm.spec \
    macros.make.in \
    README

dist_bin_SCRIPTS	= \
    ensureLdmhomeLinks \
    ensureVar \
    ensureSyslogEntry \
    ensureRpcEntry \
    ensureServicesEntry

distName = $(PACKAGE)-$(VERSION)
distArchive = $(distName).tar.gz
DISTCLEANFILES	= *.log $(distArchive)
distuninstallcheck_listfiles = find . -type f -print | egrep -v './etc/|./var/'

all:

install-data-hook:	ensure-ldmhome-links ensure-var
if INCLUDE_ROOT_ACTIONS
	@echo
	@echo "\
The next step will perform those installation actions that must be executed \
by the superuser. \
If this step doesn't work (for example, if you don't enter \
root's password), then the superuser will have to manually execute the \
command \"$(MAKE) root-actions\"." \
	| fmt >/dev/tty
	@echo >/dev/tty
if HAVE_SUDO
	$(SUDO) $(MAKE) $(AM_MAKEFLAGS) root-actions 2>/dev/tty
else
if HAVE_SU
	@printf "Enter root's password (or don't): " >/dev/tty
	@$(SU) root -c 'PATH='$$PATH' $(MAKE) $(AM_MAKEFLAGS) root-actions' \
	    </dev/tty 2>/dev/tty
	@echo >/dev/tty
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

# "ls" is used in the following instead of "test -e" because if "share" is a
# symbolic link to "runtime/share" and "runtime/share" doesn't exist, then
# "test -e share" is false.
ensure-ldmhome-links:
	$(srcdir)/ensureLdmhomeLinks $(LDMHOME) $(distName)

ensure-var:	$(DESTDIR)$(localstatedir)
	$(srcdir)/ensureVar $(DESTDIR)$(LDMHOME) $(DESTDIR)$(QUEUE_DIR) \
	    $(DESTDIR)$(LOGS_DIR) $(DESTDIR)$(DATA_DIR) 

$(DESTDIR)$(localstatedir):
	$(MKDIR_P) $@

root-actions:	\
    install_setuids \
    ensure-syslog-entry \
    ensure-rpc-entry \
    ensure-services-entry

install_setuids:
	if chown root $(DESTDIR)$(bindir)/hupsyslog && \
		chown root $(DESTDIR)$(bindir)/ldmd && \
		chmod 4755 $(DESTDIR)$(bindir)/hupsyslog && \
		chmod 4755 $(DESTDIR)$(bindir)/ldmd; then \
	    if ls -l $(DESTDIR)$(bindir)/ldmd | grep root >/dev/null && \
		ls -l $(DESTDIR)$(bindir)/ldmd | grep rws >/dev/null && \
		ls -l $(DESTDIR)$(bindir)/hupsyslog | \
		    grep root >/dev/null && \
		ls -l $(DESTDIR)$(bindir)/hupsyslog | grep rws >/dev/null; \
	    then \
		: true; \
	    else \
		echo; \
		echo " \
ERROR: The LDM server program (bin/ldmd) or the hupsyslog(1) program \
(bin/hupsyslog) is not owned by \"root\" or does not have the setuid bit \
enabled. The superuser will have to set these attributes manually."; \
		echo; \
		exit 1; \
	    fi \
	fi

ensure-syslog-entry:
	$(srcdir)/ensureSyslogEntry $(LOG_LDM) $(SYSLOG_CONF) $(LDM_LOGFILE)

ensure-rpc-entry:
	$(srcdir)/ensureRpcEntry

ensure-services-entry:
	$(srcdir)/ensureServicesEntry

install-data-hook:		$(DESTDIR)$(mandir)/$(WHATIS)
$(DESTDIR)$(mandir)/$(WHATIS):	$(DESTDIR)$(mandir)
	@if test "$(MAKEWHATIS_CMD)"; then \
	    touch $@; \
	    eval $(MAKEWHATIS_CMD) || \
		echo 1>&2 "Couldn't build manual-page database"; \
	fi

if MAINTAINER
FTPDIR	= /web/ftp/pub/ldm

remote-checks:	dist
	cd test && $(MAKE) $(AM_MAKEFLAGS) $@

commit-check:	Makefile
	if git status -a >/dev/null; then \
	    echo 1>&2 'Package needs "git commit -a"'; \
	    exit 1; \
	fi

tag:
	git tag -f v$(VERSION)

ftp:		commit-check tag dist
	filename=$(PACKAGE)-$(VERSION).tar.gz \
	dest=$(FTPDIR)/$$filename \
	&& cp $$filename $$dest \
	&& chmod u+rw,g+rw,o=r $$dest

beta:
	$(MAKE) ftp FTPDIR=$(FTPDIR)/beta
	cd html && $(MAKE) web-update

alpha:
	$(MAKE) ftp FTPDIR=/web/content/staff/steve/ldm
	cd html && $(MAKE) alpha ALPHADIR=/web/content/staff/steve/ldm

web-update:
	cd html && $(MAKE) web-update

available:	ftp web-update
endif

TOP_BUILD_DIR	= /tmp/rpmbuild
rpm:		dist ldm.spec
	rm -rf $(TOP_BUILD_DIR)
	mkdir -p $(TOP_BUILD_DIR)/SOURCES
	cp $(distArchive) $(TOP_BUILD_DIR)/SOURCES 
	mkdir $(TOP_BUILD_DIR)/SPECS
	cp ldm.spec $(TOP_BUILD_DIR)/SPECS
	rpmbuild --define '_topdir $(TOP_BUILD_DIR)' -v -bb ldm.spec

.PHONY:		\
    alpha \
    available \
    beta \
    commit-check \
    ensure-ldmhome-links \
    ensure-rpc-entry \
    ensure-services-entry \
    ensure-syslog-entry \
    ensure-var \
    ftp \
    install_setuids \
    remote-checks \
    root-actions \
    rpm \
    tag \
    web-update
