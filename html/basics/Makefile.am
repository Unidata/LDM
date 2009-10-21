docdir		= @docdir@/basics

EXTRA_DIST	= \
    activating.html \
    become_ldm.html \
    behavior.html \
    cluster.html \
    configuring.html \
    data-product.html \
    ERE.html \
    generic-LDM.html \
    glindex.html \
    index.html \
    ldmd.conf.html \
    LDM-user.html \
    logfile-format.html \
    logfile.html \
    monitoring.html \
    multiple_ldms.html \
    newlog.html \
    platform.html \
    pqact.conf.html \
    preinstallation.html \
    product-queue.html \
    programs.html \
    protocol.html \
    scour.conf.html \
    source-install-steps.html.in \
    source_or_binary.html \
    startStop.html \
    support.html \
    tips.html \
    tradeoffs.html \
    versionId.html \
    vet-ldmadmin.html \
    Downstream_6.4_Alternate.gif \
    Downstream_6.4_Primary.gif \
    downstream6.gif \
    generic-LDM.gif \
    Upstream_6.4_Alternate.gif \
    Upstream_6.4_Primary.gif \
    upstream6.gif \
    feedtypes/conduit.html \
    feedtypes/fnexrad.html \
    feedtypes/gem.html \
    feedtypes/hds.html \
    feedtypes/index.html \
    feedtypes/nexrad-fonx.html \
    feedtypes/ngraph.html \
    feedtypes/ngrid.html \
    feedtypes/nimage.html \
    feedtypes/nogaps_coamps.html \
    feedtypes/ntext.html \
    feedtypes/public_bulletins.html \
    feedtypes/wsi.html
html_DATA	= \
    activating.html \
    become_ldm.html \
    behavior.html \
    cluster.html \
    configuring.html \
    data-product.html \
    ERE.html \
    generic-LDM.html \
    glindex.html \
    index.html \
    ldmd.conf.html \
    LDM-user.html \
    logfile-format.html \
    logfile.html \
    monitoring.html \
    multiple_ldms.html \
    newlog.html \
    platform.html \
    pqact.conf.html \
    preinstallation.html \
    product-queue.html \
    programs.html \
    protocol.html \
    scour.conf.html \
    source-install-steps.html \
    source_or_binary.html \
    startStop.html \
    support.html \
    tips.html \
    tradeoffs.html \
    versionId.html \
    vet-ldmadmin.html \
    Downstream_6.4_Alternate.gif \
    Downstream_6.4_Primary.gif \
    downstream6.gif \
    generic-LDM.gif \
    Upstream_6.4_Alternate.gif \
    Upstream_6.4_Primary.gif \
    upstream6.gif
nobase_html_DATA	= \
    feedtypes/conduit.html \
    feedtypes/fnexrad.html \
    feedtypes/gem.html \
    feedtypes/hds.html \
    feedtypes/index.html \
    feedtypes/nexrad-fonx.html \
    feedtypes/ngraph.html \
    feedtypes/ngrid.html \
    feedtypes/nimage.html \
    feedtypes/nogaps_coamps.html \
    feedtypes/ntext.html \
    feedtypes/public_bulletins.html \
    feedtypes/wsi.html

all:

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    *.gif \
	    feedtypes \
	    conan:/content/software/ldm/$(PACKAGE)-$(VERSION)/basics
