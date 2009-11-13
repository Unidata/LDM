docdir		= @docdir@/basics

EXTRA_DIST	= \
    activating.html.in \
    become_ldm.html \
    behavior.html \
    cluster.html \
    configuring.html \
    data-product.html \
    ERE.html \
    failures.html \
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
    successes.html \
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
    failures.html \
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
    successes.html \
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

if MAINTAINER

successes.html:	$(top_srcdir)/test/results.tab
	sort -t , -k 2,2 -k 3,3 -k 4,4 -k 5,5 -k 6,6 -k 7,7 $< \
	| awk -F , 'BEGIN { \
		print "<!doctype html public \"-//w3c//dtd html 4.0 transitional//en\">"; \
		print "<html>"; \
		print "  <head>"; \
		print "    <meta name=\"UIINDEX\" content=\"4\">"; \
	        print "    <meta name=\"BOOKMARK\" content=\"Successful Build Environments\">"; \
	        print "    <meta name=\"AUTHOR\" content=\"steve\">"; \
	        print "    <meta name=\"KEYWORDS\" content=\"LDM, build, source\">"; \
	        print "    <title>Successful Build Environments</title>"; \
		print "  </head>"; \
		print "  <body>"; \
		print "    <h1 align=\"center\">Successful Build Environments</h1>"; \
		print "    <table align=\"center\" border=\"2\" cellpadding=\"6\" cellspacing=\"0\">"; \
		print "      <tr>"; \
		print "        <th>O/S</th>"; \
		print "        <th>CPU</th>"; \
		print "        <th>Environment Variable(s)</th>"; \
		print "        <th>configure(1) Option</th>"; \
		print "        <th>make(1)</th>"; \
		print "        <th>C Compiler</th>"; \
		print "      </tr>"; \
	    } \
	    $$8 != 0 { \
		print "      <tr>"; \
		print "        <td>" $$2 "</td>"; \
		print "        <td>" $$3 "</td>"; \
		print "        <td>" $$4 "</td>"; \
		print "        <td>" $$7 "</td>"; \
		print "        <td>" $$5 "</td>"; \
		print "        <td>" $$6 "</td>"; \
		print "      </tr>"; \
	    } \
	    END { \
		print "    </table>"; \
		print "  </body>"; \
		print "</html>"; \
	    }' >$@

failures.html:	$(top_srcdir)/test/results.tab
	sort -t , -k 2,2 -k 3,3 -k 4,4 -k 5,5 -k 6,6 -k 7,7 $< \
	| awk -F , 'BEGIN { \
		print "<!doctype html public \"-//w3c//dtd html 4.0 transitional//en\">"; \
		print "<html>"; \
		print "  <head>"; \
		print "    <meta name=\"UIINDEX\" content=\"4\">"; \
	        print "    <meta name=\"BOOKMARK\" content=\"Unsuccessful Build Environments\">"; \
	        print "    <meta name=\"AUTHOR\" content=\"steve\">"; \
	        print "    <meta name=\"KEYWORDS\" content=\"LDM, build, source\">"; \
	        print "    <title>Unsuccessful Build Environments</title>"; \
		print "  </head>"; \
		print "  <body>"; \
		print "    <h1 align=\"center\">Unsuccessful Build Environments</h1>"; \
		print "    <table align=\"center\" border=\"2\" cellpadding=\"6\" cellspacing=\"0\">"; \
		print "      <tr>"; \
		print "        <th>O/S</th>"; \
		print "        <th>CPU</th>"; \
		print "        <th>Environment Variable(s)</th>"; \
		print "        <th>configure(1) Option</th>"; \
		print "        <th>make(1)</th>"; \
		print "        <th>C Compiler</th>"; \
		print "      </tr>"; \
	    } \
	    $$8 == 0 { \
		print "      <tr>"; \
		print "        <td>" $$2 "</td>"; \
		print "        <td>" $$3 "</td>"; \
		print "        <td>" $$4 "</td>"; \
		print "        <td>" $$7 "</td>"; \
		print "        <td>" $$5 "</td>"; \
		print "        <td>" $$6 "</td>"; \
		print "      </tr>"; \
	    } \
	    END { \
		print "    </table>"; \
		print "  </body>"; \
		print "</html>"; \
	    }' >$@

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    *.gif \
	    feedtypes \
	    conan:/content/software/ldm/$(PACKAGE)-$(VERSION)/basics

endif
