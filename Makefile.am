# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

LDMHOME				= @LDMHOME@
LOGS_DIR			= @LOGS_DIR@
QUEUE_DIR			= @QUEUE_DIR@
LOG_LDM				= @LOG_LDM@
SYSLOG_CONF			= @SYSLOG_CONF@
LDM_LOGFILE			= @LDM_LOGFILE@
SUDO				= @SUDO@
SU				= @SU@
PQ_SUBDIR			= @PQ_SUBDIR@

# Keep the value of LDMHOME consonant with that in `CI-commit.sh`.
# "--with-multicast" is conditional because Spock's g++ is 4.6.3 and doesn't
# support the C++11 standard (in particular, `std::this_thread::sleep_for()`),
# which is used by `mcast_lib/fmtp/FMTPV3/RateShaper/RateShaper.cpp`.
AM_DISTCHECK_CONFIGURE_FLAGS	= --enable-debug \
                                  --disable-root-actions \
				  --with-noaaport \
				  --with-retrans \
				  --with-ingester \
				  --with-gribinsert \
				  LDMHOME=`cd ../_inst && pwd` \
			  	  @DISTCHECK_CONFIGURE_FLAGS_WITH_MULTICAST@
# This works and doesn't result in `redefined` warnings but does rely
# inappropriately on `../_inst` being the `make distcheck` installation
# directory.
#				  LDMHOME=`cd ../_inst && pwd`
# This works though it does result in LDMHOME being redefined because it's used
# in `-D` compilation flags and is also in `config.h`
#				  LDMHOME='$$''(prefix)'
# This results in '$$prefix'
#				  LDMHOME='$$$$prefix'
# This results in '$refix'
#				  LDMHOME='$$$prefix'

# Order is important in the following because of compile and link dependencies
# amongst the subdirectories.
SUBDIRS		= rpc log protocol pqinglib
if WANT_FAUX_PQ
    SUBDIRS	+= fauxPq
else
    SUBDIRS	+= pq
endif
SUBDIRS		+= registry misc protocol2
if WANT_NOAAPORT
    SUBDIRS	+= grib2 gempak
else
if WANT_GRIBINSERT
    SUBDIRS	+= grib2 gempak
endif
endif
if WANT_MULTICAST
    SUBDIRS	+= mcast_lib OESS
endif
if WANT_RETRANS
    SUBDIRS	+= noaaport/retrans
endif
SUBDIRS 	+= lib
if WANT_NOAAPORT
    SUBDIRS	+= noaaport
endif
if WANT_INGESTER
    SUBDIRS	+= fileIngest
endif
SUBDIRS 	+= regutil \
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
                   rtstats \
                   scour \
                   scripts \
                   uldbutil \
                   ulogger
SUBDIRS		+= delayQueue
if WANT_GRIBINSERT
    SUBDIRS	+= gribinsert
endif
if WANT_MULTICAST
    SUBDIRS	+= mldm_sender
endif
SUBDIRS		+= html test

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
    README

dist_bin_SCRIPTS	= \
    ensureLdmhomeLinks \
    ensureVar \
    ensureSyslogEntry \
    ensureRpcEntry \
    ensureServicesEntry \
    rpmUserActions

dist_doc_DATA		= CHANGE_LOG COPYRIGHT README

distName = $(PACKAGE)-$(VERSION)
distArchive = $(distName).tar.gz
distuninstallcheck_listfiles = find . -type f -print | egrep -v './etc/|./var/'
LAST_VERSION	= $$(git tag -l v\* | sort -t . -k 1.2n,1 -k 2n,2 -k 3n,3 -k 4n,4 | tail -1)
ACLOCAL_AMFLAGS = -I m4

all:

multicast-check:
	cd mcast_lib && $(MAKE) check
	cd mldm_sender && $(MAKE) check

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
	$(srcdir)/ensureVar $(DESTDIR)$(LDMHOME) $(DESTDIR)$(localstatedir)

$(DESTDIR)$(localstatedir):
	$(MKDIR_P) $@

ROOT_ACTIONS	= install_setuids ensure-rpc-entry ensure-services-entry
if WANT_ULOG
ROOT_ACTIONS	+= ensure-syslog-entry 
endif
root-actions:	$(ROOT_ACTIONS)

# NB: chmod(1) must follow chown(1); otherwise, the setuid bit is disabled
install_setuids:
	chown root $(DESTDIR)$(bindir)/ldmd
	chmod 4754 $(DESTDIR)$(bindir)/ldmd
	chown root $(DESTDIR)$(bindir)/hupsyslog
	chmod 4754 $(DESTDIR)$(bindir)/hupsyslog
	-if test -f $(DESTDIR)$(bindir)/noaaportIngester; then \
	    chown root $(DESTDIR)$(bindir)/noaaportIngester; \
	    chmod 4754 $(DESTDIR)$(bindir)/noaaportIngester; \
	fi
	-if test -f $(DESTDIR)$(bindir)/fileIngester; then \
	    chown root $(DESTDIR)$(bindir)/fileIngester; \
	    chmod 4754 $(DESTDIR)$(bindir)/fileIngester; \
	fi
	-if test -f $(DESTDIR)$(bindir)/dvbs_multicast; then \
	    chown root $(DESTDIR)$(bindir)/dvbs_multicast; \
	    chmod 4754 $(DESTDIR)$(bindir)/dvbs_multicast; \
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
ldmd, hupsyslog, noaaportIngester, fileIngester, dvbs_multicast. The superuser \
will have to set these attributes manually."; \
	    echo; \
	    exit 1; \
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
	$(MAKE) configure Makefile timestamp dist commitAndTag rpm

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

ftp-actual:		dist rpm $(FTPDIR)
	cp -p $(distArchive) $(FTPDIR)
	chmod u+rw,g+rw,o=r $(FTPDIR)/$(distArchive)
	rm -f $(FTPDIR)/$(PACKAGE).tar.gz
	$(LN_S) $(distArchive) $(FTPDIR)/$(PACKAGE).tar.gz
	cp -p $(PACKAGE)*-$(VERSION)-*.rpm $(FTPDIR)
	chmod u+rw,g+rw,o=r $(FTPDIR)/$(PACKAGE)*-$(VERSION)-*.rpm 

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
    multicast-check \
    release \
    remote-checks \
    root-actions \
    rpm \
    rpmlint \
    tag \
    timestamp \
    web-update
