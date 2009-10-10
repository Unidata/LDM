# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

DISTCHECK_CONFIGURE_FLAGS	= --enable-ldmhome=$(prefix)
include_HEADERS		= ldmconfig.h

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
    doc

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

all:

#LIBRARY	= libldm.a
#
#INSTALL_TARGETS = \
#	misc/install \
#	protocol/install \
#	$(pq_dir)/install \
#	pqcheck/install \
#	pqcreate/install \
#	pqinsert/install \
#	pqcat/install \
#	pqexpire/install \
#	pqmon/install \
#	pqutil/install \
#	pqsend/install \
#	pqing/install \
#	pqact/install \
#	pqsurf/install \
#	send/install \
#	rtstats/install \
#	feedme/install \
#	notifyme/install \
#	server/install \
#	ldmping/install \
#	scripts/install \
#	scour/install \
#	regex/install
#
#INSTALL_SETUIDS_TARGETS = \
#	ulog/install_setuids \
#	server/install_setuids
#
#CLEAN_TARGETS = \
#	misc/clean \
#	protocol/clean \
#	pq/clean \
#	fauxPq/clean \
#	pqcheck/clean \
#	pqcreate/clean \
#	pqinsert/clean \
#	pqcat/clean \
#	pqexpire/clean \
#	pqmon/clean \
#	pqutil/clean \
#	pqsend/clean \
#	pqing/clean \
#	pqact/clean \
#	pqsurf/clean \
#	send/clean \
#	rtstats/clean \
#	feedme/clean \
#	notifyme/clean \
#	server/clean \
#	ldmping/clean \
#	scripts/clean \
#	scour/clean \
#	regex/clean \
#	rpc/clean
#
#DISTCLEAN_TARGETS = \
#	misc/distclean \
#	protocol/distclean \
#	pq/distclean \
#	fauxPq/distclean \
#	pqcheck/distclean \
#	pqcreate/distclean \
#	pqinsert/distclean \
#	pqcat/distclean \
#	pqexpire/distclean \
#	pqmon/distclean \
#	pqutil/distclean \
#	pqsend/distclean \
#	pqing/distclean \
#	pqact/distclean \
#	pqsurf/distclean \
#	send/distclean \
#	rtstats/distclean \
#	feedme/distclean \
#	notifyme/distclean \
#	server/distclean \
#	ldmping/distclean \
#	scripts/distclean \
#	scour/distclean \
#	regex/distclean \
#	rpc/distclean
#
#SUBDIRS = rpc $(LIBSUBDIRS) $(PROGSUBDIRS) doc
#
#TAG_SRCS	= \
#	ulog/*.h ulog/*.c \
#	protocol/*.c protocol/*.h \
#	server/*.c server/*.h \
#	misc/*.c misc/*.h \
#	pq/*.c pq/*.h
#
## inventory
#PACKING_LIST = \
#	ANNOUNCEMENT \
#	configure \
#	COPYRIGHT \
#	CHANGE_LOG \
#	macros.make.in \
#	Makefile \
#	MANIFEST \
#	README \
#	RELEASE_NOTES \
#	rules.make \
#	simple_program.make \
#	VERSION
#
## $(LIBRARY) must be made first because programs built
## in subdirectories link against it.
#all:		$(LIBRARY) rpc/all $(ALL_TARGETS)
#
#$(LIBRARY):	ldm_version.o
#	$(AR) $(ARFLAGS) $(LIBRARY) ldm_version.o
#	$(RANLIB) $(LIBRARY)
#
#ldm_version.o:	ldm_version.c
#	$(CC) -c $(CPPFLAGS) $(CFLAGS) ldm_version.c
#
#test:		$(TEST_TARGETS)
#
#install:	$(LIBDIR)/$(LIBRARY) $(INSTALL_TARGETS) whatis
#
#whatis:		$(MANDIR)/$(WHATIS)
#
#$(MANDIR)/$(WHATIS):	$(MANDIR)
#	@if test "$(MAKEWHATIS_CMD)"; then \
#	    touch $@; \
#	    eval $(MAKEWHATIS_CMD) || \
#		echo 1>&2 "Couldn't build manual-page database"; \
#	fi
#
#install_setuids:	$(INSTALL_SETUIDS_TARGETS)
#
#clean:		$(CLEAN_TARGETS)
#	rm -f *.a *.o *.ln *.i $(LIBRARY) ldm_version.c
#
#distclean:	$(DISTCLEAN_TARGETS)
#	rm -f *.a *.o *.ln *.i $(LIBRARY) ldm_version.c
#	rm -f config.cache config.status *.log MANIFEST *.Z macros.make
#
#ldm_version.c: VERSION
#	echo 'const char* ldm_version = "'`cat VERSION`'";' >$@
#
#$(ALL_TARGETS) \
#$(TEST_TARGETS) \
#$(INSTALL_TARGETS) \
#$(INSTALL_SETUIDS_TARGETS) \
#$(CLEAN_TARGETS) \
#$(DISTCLEAN_TARGETS):
#	@subdir=`echo $@ | sed 's,/.*,,'`; \
#	target=`echo $@ | sed 's,.*/,,'`; \
#	$(MAKE) $(MFLAGS) SUBDIR=$$subdir TGET=$$target subdir_target
#
#rpc/all:
#	@echo ""
#	@cd rpc && \
#	    echo "Making \`all' in directory `pwd`" && \
#	    echo "" && \
#	    $(MAKE) all LIBRARY=../$(LIBRARY) || exit 1
#	@echo ""
#	@echo "Returning to directory `pwd`"
#	@echo ""
#
#subdir_target:
#	@echo ""
#	@cd $(SUBDIR) && \
#	    echo "Making \`$(TGET)' in directory `pwd`" && \
#	    echo "" && \
#	    $(MAKE) $(TGET) || exit 1
#	@echo ""
#	@echo "Returning to directory `pwd`"
#	@echo ""
#
#$(LIBDIR)/$(LIBRARY) :	$(LIBDIR) FORCE
#	-@cmp -s $(LIBRARY) $@ || (cp $(LIBRARY) $@ && \
#	echo 'updated $@')
#
#$(LIBDIR):
#	mkdir $@
#
#configure: configure.in aclocal.m4
#	autoconf
#
#config.status: configure
#	-@if [ -f "$@" ]; then \
#		$(SHELL) config.status --recheck ; \
#	else \
#		echo 1>&1 "$@ must be created by the configure script"; \
#		exit 1; \
#	fi
#
#subdirs:
#	@echo $(SUBDIRS)
#
## Ultimately depends on all the Makefiles, but...
#MANIFEST: Makefile FORCE
#	@echo 1>&2 Creating MANIFEST
#	@$(MAKE) -s $(LOCAL_MACROS) TOP_MANIFEST.echo | sort -o $@
#
#TOP_MANIFEST.echo:
#	@echo $(PACKING_LIST) | fmt -1
#	@if [ -n "$(SUBDIRS)" ]; then \
#	    subdirs="$(SUBDIRS)"; \
#	    for subdir in $$subdirs; do \
#		(cd $$subdir && \
#		echo 1>&2 Creating MANIFEST.echo in `pwd` && \
#		$(MAKE) $(MFLAGS) $(SUBDIR_MACROS) MANIFEST.echo | \
#		    sed "s|^|$$subdir/|") || exit 1; \
#	    done; \
#	else \
#	    :; \
#	fi
#
#commit-check:	FORCE
#	if git status -a >/dev/null; then \
#	    echo 1>&2 'Package needs "git commit -a"'; \
#	    exit 1; \
#	fi
#
#tag:	FORCE
#	git tag -f v$(VERSION)
#
#tar.gz:	FORCE commit-check tag
#	tag=v$(VERSION) \
#	id=$(PACKAGE)-$(VERSION); \
#	prefix=/tmp/$$id; \
#	rm -rf $$prefix; \
#	srcdir=$$prefix/src; \
#	if git clone . $$srcdir >/dev/null; then \
#	    rm -rf $$srcdir/.git; \
#	    pax -x ustar -w -s ':/tmp/::' $$srcdir | gzip -c >$$id.$@; \
#	    status=$$?; \
#	    rm -rf $$prefix; \
#	    exit $$status; \
#	fi
#
#ftp:	FORCE commit-check tag tar.gz
#	filename=$(PACKAGE)-$(VERSION).tar.gz \
#	&& scp $$filename ftp:$(FTPDIR) \
#	&& ssh ftp chmod u+rw,g+rw,o=r $(FTPDIR)/$$filename
#
#beta:	FORCE commit-check tag tar.gz
#	filename=$(PACKAGE)-$(VERSION).tar.gz \
#	&& scp $$filename ftp:$(FTPDIR)/beta \
#	&& ssh ftp chmod u+rw,g+rw,o=r $(FTPDIR)/beta/$$filename
#
#FORCE:
#
#ldm_version.o:	ldm_version.c
