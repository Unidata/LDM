SUBDIRS		= basics tutorial
EXTRA_DIST	= \
    compatibility.html \
    copyright.html \
    factsheet.html \
    generalHelp.html \
    glossary.html \
    history.html \
    howto-nnexrad.html \
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
html_DATA	= \
    CHANGE_LOG \
    compatibility.html \
    copyright.html \
    factsheet.html \
    generalHelp.html \
    glossary.html \
    history.html \
    howto-nnexrad.html \
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
BUILT_SOURCES	= CHANGE_LOG
CLEANFILES	= CHANGE_LOG

if MAINTAINER
web-update:	commit-check FORCE
	rsync $(RSYNC_FLAGS) -u \
	    history.html \
	    factsheet.html \
	    generalHelp.html \
	    conan:/content/software/ldm
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    README \
	    ../CHANGE_LOG \
	    *.html \
	    install \
	    troubleshooting \
	    conan:/content/software/ldm/$(PACKAGE)-$(VERSION)
	cd basics && make $@
	cd tutorial && make $@
	cd workshop && make $@

.PHONY:		web-update FORCE

FORCE:
endif

CHANGE_LOG:
	cp $(top_srcdir)/$@ $@
