htmldir		= $(docdir)
SUBDIRS		= basics tutorial workshop
html_DATA	= \
    CHANGE_LOG \
    COPYRIGHT \
    compatibility.html \
    factsheet.html \
    generalHelp.html \
    glossary.html \
    huge-queues.html \
    index.html \
    known-problems.html \
    LDM_icon.GIF \
    ldmConfigInstallList.html \
    ldmfaq.html \
    manindex.html \
    networkSecurityAndSetup.html \
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
    compatibility.html \
    factsheet.html \
    generalHelp.html \
    glossary.html \
    huge-queues.html \
    index.html \
    known-problems.html \
    ldmConfigInstallList.html \
    ldmfaq.html \
    manindex.html \
    networkSecurityAndSetup.html \
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
BUILT_SOURCES	= CHANGE_LOG COPYRIGHT
CLEANFILES	= CHANGE_LOG COPYRIGHT

WEBHOST		= www.unidata.ucar.edu
ALPHADIR	= /web/content/staff/steve/ldm
WEB_DESTDIR	= /web/content/software/ldm/$(PACKAGE)-$(VERSION)

commit-check:	Makefile
	if ! git status -u >/dev/null; then \
	    echo 1>&2 'Package needs "git commit -a"'; \
	    exit 1; \
	fi

web-update:	commit-check
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    ../README \
	    ../CHANGE_LOG \
	    ../COPYRIGHT \
	    *.html \
	    troubleshooting \
	    $(WEBHOST):$(WEB_DESTDIR)
	cd basics && make $@ WEBHOST=$(WEBHOST) WEB_DESTDIR=$(WEB_DESTDIR)
	cd tutorial && make $@ WEBHOST=$(WEBHOST) WEB_DESTDIR=$(WEB_DESTDIR)
	cd workshop && make $@ WEBHOST=$(WEBHOST) WEB_DESTDIR=$(WEB_DESTDIR)

alpha:
	$(MAKE) web-update WEB_DESTDIR=$(ALPHADIR)

.PHONY:		commit-check alpha web-update

CHANGE_LOG COPYRIGHT:
	ln -s $(top_srcdir)/$@ $@
