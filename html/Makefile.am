htmldir		= $(docdir)
SUBDIRS		= basics tutorial workshop
html_DATA	= \
    CHANGE_LOG \
    compatibility.html \
    copyright.html \
    factsheet.html \
    generalHelp.html \
    glossary.html \
    history.html \
    huge-queues.html \
    index.html \
    known-problems.html \
    LDM_and_CVS.html \
    LDM_icon.GIF \
    ldmConfigInstallList.html \
    LDM_Development_Steps.html \
    ldmfaq.html \
    manindex.html \
    networkSecurityAndSetup.html \
    release-notes.html \
    rtstats-description.html \
    subscribe_ldm-users.html
nobase_html_DATA	= \
    troubleshooting/index.html \
    troubleshooting/networkTrouble.html \
    troubleshooting/pqmon.html \
    troubleshooting/reclassDoc.html \
    troubleshooting/packetshaper/addpart.gif \
    troubleshooting/packetshaper/addpart.jpg \
    troubleshooting/packetshaper/addpolicy.gif \
    troubleshooting/packetshaper/addpolicy.jpg \
    troubleshooting/packetshaper/manageLDM.gif \
    troubleshooting/packetshaper/manageLDM.jpg \
    troubleshooting/packetshaper/managepage.gif \
    troubleshooting/packetshaper/managepage.jpg \
    troubleshooting/packetshaper/newclass2.gif \
    troubleshooting/packetshaper/newclass2.jpg \
    troubleshooting/packetshaper/newclass.gif \
    troubleshooting/packetshaper/newclass.jpg \
    troubleshooting/packetshaper/packetshaper.html \
    troubleshooting/packetshaper/traffictest.gif \
    troubleshooting/packetshaper/traffictest.jpg
EXTRA_DIST	= \
    CHANGE_LOG \
    compatibility.html \
    copyright.html \
    factsheet.html \
    generalHelp.html \
    glossary.html \
    history.html \
    huge-queues.html \
    index.html \
    known-problems.html \
    LDM_and_CVS.html \
    ldmConfigInstallList.html \
    LDM_Development_Steps.html \
    ldmfaq.html \
    manindex.html \
    networkSecurityAndSetup.html \
    release-notes.html \
    rtstats-description.html \
    subscribe_ldm-users.html \
    LDM_icon.GIF \
    troubleshooting/index.html \
    troubleshooting/networkTrouble.html \
    troubleshooting/pqmon.html \
    troubleshooting/reclassDoc.html \
    troubleshooting/packetshaper/addpart.gif \
    troubleshooting/packetshaper/addpart.jpg \
    troubleshooting/packetshaper/addpolicy.gif \
    troubleshooting/packetshaper/addpolicy.jpg \
    troubleshooting/packetshaper/manageLDM.gif \
    troubleshooting/packetshaper/manageLDM.jpg \
    troubleshooting/packetshaper/managepage.gif \
    troubleshooting/packetshaper/managepage.jpg \
    troubleshooting/packetshaper/newclass2.gif \
    troubleshooting/packetshaper/newclass2.jpg \
    troubleshooting/packetshaper/newclass.gif \
    troubleshooting/packetshaper/newclass.jpg \
    troubleshooting/packetshaper/packetshaper.html \
    troubleshooting/packetshaper/traffictest.gif \
    troubleshooting/packetshaper/traffictest.jpg
BUILT_SOURCES	= CHANGE_LOG
CLEANFILES	= CHANGE_LOG

if MAINTAINER
WEB_DESTDIR	= /web/content/software/ldm

commit-check:
	if git status -a >/dev/null; then \
	    echo 1>&2 'Package needs "git commit -a"'; \
	    exit 1; \
	fi

web-update:	commit-check
	rsync $(RSYNC_FLAGS) -u \
	    history.html \
	    factsheet.html \
	    generalHelp.html \
	    $(WEB_DESTDIR)
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    README \
	    ../CHANGE_LOG \
	    *.html \
	    install \
	    troubleshooting \
	    $(WEB_DESTDIR)/$(PACKAGE)-$(VERSION)
	cd basics && make $@ WEB_DESTDIR=$(WEB_DESTDIR)/$(PACKAGE)-$(VERSION)
	cd tutorial && make $@ WEB_DESTDIR=$(WEB_DESTDIR)/$(PACKAGE)-$(VERSION)
	cd workshop && make $@ WEB_DESTDIR=$(WEB_DESTDIR)/$(PACKAGE)-$(VERSION)

alpha:
	$(MAKE) web-update WEB_DESTDIR=/web/content/staff/steve/ldm

.PHONY:		commit-check alpha web-update
endif

CHANGE_LOG:
	cp $(top_srcdir)/$@ $@
