# Copyright 2013 University Corporation for Atmospheric Research
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

DISTCHECK_CONFIGURE_FLAGS	= --disable-root-actions --with-noaaport --with-gribinsert

LIB_SUBDIRS	= \
    rpc \
    ulog \
    misc \
    pqinglib \
    protocol \
    registry
if WANT_FAUX_PQ
    LIB_SUBDIRS	+= fauxPq
else
    LIB_SUBDIRS	+= pq
endif
if WANT_NOAAPORT
    LIB_SUBDIRS	+= grib2 gempak
else
if WANT_GRIBINSERT
    LIB_SUBDIRS	+= grib2 gempak
endif
endif
LIB_SUBDIRS	+= lib

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
    uldbutil \
    ulogger
if WANT_NOAAPORT
    PROG_SUBDIRS	+= noaaport
endif
if WANT_GRIBINSERT
    PROG_SUBDIRS	+= gribinsert
endif

SUBDIRS		= \
    $(LIB_SUBDIRS) \
    $(PROG_SUBDIRS) \
    html test

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
    
LDM_ICON_FILE		= ldm-50x50.xpm

EXTRA_DIST = \
    $(LDM_ICON_FILE) \
    CHANGE_LOG \
    COPYRIGHT \
    extractDecls \
    ldm.spec \
    ldm.spec.in \
    macros.make.in \
    README

dist_bin_SCRIPTS	= \
    ensureLdmhomeLinks \
    ensureVar \
    ensureSyslogEntry \
    ensureRpcEntry \
    ensureServicesEntry \
    rpmUserActions

distName = $(PACKAGE)-$(VERSION)
distArchive = $(distName).tar.gz
CLEANFILES	= *.log
distuninstallcheck_listfiles = find . -type f -print | egrep -v './etc/|./var/'
LAST_VERSION	= $$(git tag -l v\* | sort -t . -k 1.2n,1 -k 2n,2 -k 3n,3 -k 4n,4 | tail -1)

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

ensure-ldmhome-links:
	$(srcdir)/ensureLdmhomeLinks $(DESTDIR)$(LDMHOME) $(distName)

ensure-var:	$(DESTDIR)$(localstatedir)
	$(srcdir)/ensureVar $(DESTDIR)$(LDMHOME)

$(DESTDIR)$(localstatedir):
	$(MKDIR_P) $@

root-actions:	\
    install_setuids \
    ensure-syslog-entry \
    ensure-rpc-entry \
    ensure-services-entry

install_setuids:
	chown root $(DESTDIR)$(bindir)/ldmd
	chmod 4755 $(DESTDIR)$(bindir)/ldmd
	chown root $(DESTDIR)$(bindir)/hupsyslog
	chmod 4755 $(DESTDIR)$(bindir)/hupsyslog
	-if test -f $(DESTDIR)$(bindir)/noaaportIngester; then \
	    chown root $(DESTDIR)$(bindir)/noaaportIngester; \
	    chmod 4755 $(DESTDIR)$(bindir)/noaaportIngester; \
	fi
	-if test -f $(DESTDIR)$(bindir)/dvbs_multicast; then \
	    chown root $(DESTDIR)$(bindir)/dvbs_multicast; \
	    chmod 4755 $(DESTDIR)$(bindir)/dvbs_multicast; \
	fi
	@if ls -l $(DESTDIR)$(bindir)/ldmd | grep root >/dev/null && \
	    ls -l $(DESTDIR)$(bindir)/ldmd | grep rws >/dev/null && \
	    ls -l $(DESTDIR)$(bindir)/hupsyslog | grep root >/dev/null && \
	    ls -l $(DESTDIR)$(bindir)/hupsyslog | grep rws >/dev/null; \
	then \
	    : true; \
	else \
	    echo; \
	    echo " \
ERROR: One or more of the following programs in the \"bin/\" installation \
directory is not owned by \"root\" or does not have the setuid bit enabled: \
ldmd, hupsyslog, noaaportIngester, dvbs_multicast. The superuser will have to \
set these attributes manually."; \
	    echo; \
	    exit 1; \
	fi

ensure-syslog-entry:
	$(srcdir)/ensureSyslogEntry $(LOG_LDM) $(SYSLOG_CONF) $(LDM_LOGFILE)

ensure-rpc-entry:
	$(srcdir)/ensureRpcEntry

ensure-services-entry:
	$(srcdir)/ensureServicesEntry

install-data-local:	$(DESTDIR)$(docdir)/CHANGE_LOG \
                	$(DESTDIR)$(docdir)/COPYRIGHT \
                	$(DESTDIR)$(docdir)/README
$(DESTDIR)$(docdir)/CHANGE_LOG:	CHANGE_LOG
	cp $? $@
$(DESTDIR)$(docdir)/COPYRIGHT:	COPYRIGHT
	cp $? $@
$(DESTDIR)$(docdir)/README:	README
	cp $? $@

install-data-hook:		$(DESTDIR)$(mandir)/$(WHATIS)
$(DESTDIR)$(mandir)/$(WHATIS):	$(DESTDIR)$(mandir)
	@if test "$(MAKEWHATIS_CMD)"; then \
	    touch $@; \
	    eval $(MAKEWHATIS_CMD) || \
		echo 1>&2 "Couldn't build manual-page database"; \
	fi

if MAINTAINER
FTPDIR	= /web/ftp/pub/$(PACKAGE)

$(FTPDIR):
	mkdir -p $@

remote-checks:	dist
	cd test && $(MAKE) $(AM_MAKEFLAGS) $@

timestamp:
	awk 'NR == 1 {\
		print $$1, "	'"`date --rfc-3339=seconds`"'";\
		next;\
	    }\
	    {print}' CHANGE_LOG >CHANGE_LOG.tmp
	mv CHANGE_LOG.tmp CHANGE_LOG

dist:		configure

commit-check:	Makefile
	if git status --untracked-files | grep -Fq 'nothing to commit'; \
	then \
	    : true; \
	else \
	    echo 1>&2 'Package needs "git commit --untracked-files"'; \
	    exit 1; \
	fi

commitAndTag:
	git commit --all --message="v$(VERSION)"
	git tag --force "v$(VERSION)"

tag:
	git tag --force v$(VERSION)

# Make this instead of "available" when the package won't be released to the
# public
release:
	test -z "`git status --porcelain`" || (echo Commit changes first!; exit 1)
	echo 'PUT VERSION HERE' >CHANGE_LOG.tmp
	git log --name-status --pretty=full $(LAST_VERSION).. >>CHANGE_LOG.tmp
	echo '' >>CHANGE_LOG.tmp
	cat CHANGE_LOG >>CHANGE_LOG.tmp
	vi CHANGE_LOG.tmp
	mv CHANGE_LOG.tmp CHANGE_LOG
	sed -n '1,/^%changelog/p' ldm.spec.in ldm.spec.in >ldm.spec.in.tmp
	echo >>ldm.spec.in.tmp
	newVersion=`awk '{print $$1; exit}' CHANGE_LOG`; \
	echo $$newVersion | egrep '^[0-9]+(\.[0-9]+)*$$' >/dev/null || exit 1; \
        echo "* `date '+%a %b %e %Y'` Steve Emmerson" \
        	"<support-ldm@unidata.ucar.edu> $$newVersion" >>ldm.spec.in.tmp; \
        echo "- PUT SUMMARY HERE" >>ldm.spec.in.tmp; \
	sed '1,/^%changelog/d' ldm.spec.in >>ldm.spec.in.tmp; \
	vi "+/PUT SUMMARY HERE/" ldm.spec.in.tmp; \
	mv ldm.spec.in.tmp ldm.spec.in; \
	sed '/^AC_INIT(/s/\[[0-9][0-9.]*\]/['"$$newVersion"']/' configure.ac \
	    >configure.ac.tmp; \
	mv configure.ac.tmp configure.ac
	$(MAKE) configure Makefile timestamp dist commitAndTag

ftp-actual:		dist $(FTPDIR)
	cp $(distArchive) $(FTPDIR)
	chmod u+rw,g+rw,o=r $(FTPDIR)/$(distArchive)
	rm -f $(FTPDIR)/$(PACKAGE).tar.gz
	$(LN_S) $(distArchive) $(FTPDIR)/$(PACKAGE).tar.gz

download_update:
	previous=`sed -n 's/.*"%current%" value="\(.*\)".*/\1/p' \
		 /web/content/downloads/$(PACKAGE)/toc.xml`; \
	if test "$$previous" != "$(VERSION)"; then \
	    sed -e 's/"%current%" value=".*"/"%current%" value="$(VERSION)"/' \
	    -e 's/"%previous%" value=".*"/"%previous%" value="'$$previous'"/' \
		/web/content/downloads/$(PACKAGE)/toc.xml >toc.xml.new; \
	    cp /web/content/downloads/$(PACKAGE)/toc.xml \
		/web/content/downloads/$(PACKAGE)/toc.xml.old; \
	    mv -f toc.xml.new /web/content/downloads/$(PACKAGE)/toc.xml; \
	fi

ftp:			release
	$(MAKE) ftp-actual download_update

beta:
	$(MAKE) ftp FTPDIR=$(FTPDIR)/beta
	cd html && $(MAKE) web-update

alpha:
	$(MAKE) ftp FTPDIR=/web/content/staff/steve/ldm
	cd html && $(MAKE) alpha ALPHADIR=/web/content/staff/steve/ldm

web-update:
	cd html && $(MAKE) web-update
	cd noaaport && $(MAKE) web-update

# Make this instead of "release" when the package will be released to the
# public
available:	ftp web-update

RPM_BUILDDIR	= $$HOME/rpmbuild
#
# Create binary RPM files in this directory
rpm:		dist ldm.spec
	mkdir -p $(RPM_BUILDDIR)/SOURCES
	cp -p $(distArchive) $(LDM_ICON_FILE) $(RPM_BUILDDIR)/SOURCES 
	mkdir -p $(RPM_BUILDDIR)/SPECS
	cp ldm.spec $(RPM_BUILDDIR)/SPECS
	rpmbuild -bb $(RPM_BUILDDIR)/SPECS/ldm.spec
	mv $(RPM_BUILDDIR)/RPMS/`uname -m`/$(PACKAGE)*-$(VERSION)-*.rpm .
    
rpmlint:
	rpmlint $(RPM_BUILDDIR)/SPECS/ldm.spec \
    	    $(RPM_BUILDDIR)/RPMS/x86_64/$(distName)*.rpm | egrep -v \
    	    'invalid-license|explicit-lib-dependency|devel-dependency|hardcoded-library-path|configure-without-libdir-spec|spelling-error|postin-without-ldconfig|no-documentation|use-of-home-in-%post|dangerous-command-in-%post mv|library-without-ldconfig-postun|dangling-relative-symlink|non-standard-executable-perm|non-etc-or-var-file-marked-as-conffile|unstripped-binary-or-object|manpage-not-compressed|non-standard-uid|dir-or-file-in-usr-local|devel-file-in-non-devel-package|setuid-binary|script-without-shebang'
    	
endif

.PHONY:		\
    alpha \
    available \
    beta \
    commitAndTag \
    commit-check \
    download_update \
    ensure-ldmhome-links \
    ensure-rpc-entry \
    ensure-services-entry \
    ensure-syslog-entry \
    ensure-var \
    ftp \
    ftp-actual \
    install_setuids \
    release \
    remote-checks \
    root-actions \
    rpm \
    rpmlint \
    tag \
    timestamp \
    web-update
