# Copyright 2009 University Corporation for Atmospheric Research
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

include_HEADERS		= config.h

EXTRA_DIST = \
    CHANGE_LOG \
    COPYRIGHT \
    README \
    extractDecls \
    macros.make.in

distName = $(PACKAGE)-$(VERSION)
distArchive = $(distName).tar.gz
DISTCLEANFILES	= *.log $(distArchive)
distuninstallcheck_listfiles = find . -type f -print | egrep -v './etc/|./var/'

all:

install-data-hook:	ensure-ldmhome-links ensure-var
if INCLUDE_ROOT_ACTIONS
	@echo
	@echo "\
The next step will perform those installation actions that must be \
executed by the superuser.  \
If this step doesn't work (for example, if you don't enter \
root's password), then the superuser will have to manually execute \
the command \"$(MAKE) root-actions\"." \
	| fmt >/dev/tty
	@echo >/dev/tty
if HAVE_SUDO
	$(SUDO) $(MAKE) $(AM_MAKEFLAGS) root-actions
else
if HAVE_SU
	@printf "Enter root's password (or don't): " >/dev/tty
	@$(SU) root -c 'PATH='$$PATH' $(MAKE) $(AM_MAKEFLAGS) root-actions' \
	    </dev/tty 2>/dev/null
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
	cd $(DESTDIR)$(LDMHOME) \
	&& ( ls src >/dev/null 2>&1 || $(LN_S) runtime/src src ) \
	&& ( ls bin >/dev/null 2>&1 || $(LN_S) runtime/bin bin ) \
	&& ( ls include >/dev/null 2>&1 || $(LN_S) runtime/include include ) \
	&& ( ls lib >/dev/null 2>&1 || $(LN_S) runtime/lib lib ) \
	&& ( ls share >/dev/null 2>&1 || $(LN_S) runtime/share share ) \
	&& ( test ! -e $(distName) || ( test -e runtime || \
	    $(LN_S) $(distName) runtime ) )

ensure-var:	$(DESTDIR)$(localstatedir)
	if test -e $(DESTDIR)$(LDMHOME)/data; then \
	    test -e $(DESTDIR)$(QUEUE_DIR) \
	    || $(LN_S) $(DESTDIR)$(LDMHOME)/data $(DESTDIR)$(QUEUE_DIR); \
	else \
	    test -e $(DESTDIR)$(QUEUE_DIR) || mkdir -p $(DESTDIR)$(QUEUE_DIR); \
	fi
	if test -e $(DESTDIR)$(LDMHOME)/logs; then \
	    test -e $(DESTDIR)$(LOGS_DIR) \
	    || $(LN_S) $(DESTDIR)$(LDMHOME)/logs $(DESTDIR)$(LOGS_DIR); \
	else \
	    test -e $(DESTDIR)$(LOGS_DIR) || mkdir -p $(DESTDIR)$(LOGS_DIR); \
	fi
	if test -e $(DESTDIR)$(LDMHOME)/data; then \
	    test -e $(DESTDIR)$(DATA_DIR) \
	    || $(LN_S) $(DESTDIR)$(LDMHOME)/data $(DESTDIR)$(DATA_DIR); \
	else \
	    test -e $(DESTDIR)$(DATA_DIR) || mkdir -p $(DESTDIR)$(DATA_DIR); \
	fi

$(DESTDIR)$(localstatedir):
	$(MKDIR_P) $@

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
to be manually executed by the superuser on a system that allows these \
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
endif

.PHONY:		\
    root-actions \
    install_setuids \
    ensure-syslog-entry \
    ensure-rpc-entry \
    ensure-services-entry \
    ensure-ldmhome-links \
    ensure-var \
    commit-check \
    remote-checks \
    tag \
    ftp \
    beta \
    alpha
