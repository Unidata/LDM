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
PQ_SUBDIR			= @PQ_SUBDIR@

# Keep the value of LDMHOME consonant with that in `CI-commit.sh`.
## "--with-multicast" is conditional because Spock's g++ is 4.6.3 and doesn't
## support the C++11 standard (in particular, `std::this_thread::sleep_for()`),
## which is used by `mcast_lib/fmtp/FMTPV3/RateShaper/RateShaper.cpp`.
AM_DISTCHECK_CONFIGURE_FLAGS	= --enable-debug \
				  --with-noaaport \
				  --with-retrans \
				  --with-ingester \
				  --with-gribinsert \
				  --with-delayQueue \
				  LDMHOME=`cd ../_inst && pwd`
if WANT_MULTICAST
AM_DISTCHECK_CONFIGURE_FLAGS	+= --with-multicast
endif
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
if WANT_DELAYQUEUE
    SUBDIRS	+= delayQueue
endif
if WANT_GRIBINSERT
    SUBDIRS	+= gribinsert
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

DISTCLEANFILES		= *.gz config.h
MAINTAINERCLEANFILES	= \
	aclocal.m4 \
	autom4te.cache/* \
	build-aux/* \
	check-* \
	clang-scan \
	compile \
	config.* \
	config.guess \
	configure \
	consistent \
	cov-int/* \
	cov-int/emit/* \
	depcomp \
	*.deplog \
	install-sh \
	*.log \
	ltmain.sh \
	m4/* \
	missing \
	*.tgz

all:

install-data-hook:	ensure-ldmhome-links ensure-var
	@echo
	@echo
	@echo '************************************************************************'
	@echo "NOTE: Because certain programs must be owned by root and setuid, the"
	@echo "command \"$(MAKE) root-actions\" must now be executed by the superuser (i.e.,"
	@echo "\"root\") in order to complete the installation process. If this step is"
	@echo "omitted, then the LDM won't work."
	@echo '************************************************************************'
	@echo
	@echo

ensure-ldmhome-links:
	$(srcdir)/ensureLdmhomeLinks $(DESTDIR)$(LDMHOME) `basename $(prefix)`

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
	-if test -f $(DESTDIR)$(bindir)/blender; then \
	    chown root $(DESTDIR)$(bindir)/blender; \
	    chmod 4754 $(DESTDIR)$(bindir)/blender; \
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

install-exec-hook:
	chmod g-w,o-w $(DESTDIR)$(bindir)

install-data-hook:		$(DESTDIR)$(mandir)/$(WHATIS)
$(DESTDIR)$(mandir)/$(WHATIS):	$(DESTDIR)$(mandir)
	@if test "$(MAKEWHATIS_CMD)"; then \
	    touch $@; \
	    eval $(MAKEWHATIS_CMD) || \
		echo 1>&2 "Couldn't build manual-page database"; \
	fi

distcheck-hook:
	echo distcheckdir=`pwd`
	chmod u+w $(distName)

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

#pull-oess:
#	git fetch oess
## The following command uses the wrong subdirectory
##	git merge --strategy=subtree --squash oess/master
#	git merge --strategy=subtree -X subtree=mcast_lib/OESS-Client \
#		--squash oess/master
#	git commit -m "Pulled OESS-Client"

#pull-fmtp:
#	git subtree pull --prefix=gempak/tables gempak-tables master
##	git fetch fmtp
### The following command uses the wrong subdirectory
###	git merge --strategy=subtree --squash fmtp/master
##	git merge --strategy=subtree -X subtree=mcast_lib/FMTP-LDM7 \
##		--squash fmtp/master
##	git commit -m "Pulled FMTP-LDM7"

# test
push-gempak-tables:
	git subtree push --prefix=gempak/tables gempak-tables master

push-%:
	git checkout -b backport-% %/master
	git merge --strategy=subtree master
	git push % HEAD:master
	git checkout master
	git branch -d backport-%
	
.PHONY:		\
    ensure-ldmhome-links \
    ensure-rpc-entry \
    ensure-services-entry \
    ensure-syslog-entry \
    ensure-var \
    install_setuids \
    root-actions \
    rpm \
    rpmlint \
    pull-fmtp \
    pull-oess \
    push-fmtp \
    push-oess
