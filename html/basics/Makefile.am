htmldir	= $(docdir)/basics
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
    regpar-top.html \
    scour.conf.html \
    source-install-steps.html.in \
    source_or_binary.html \
    startStop.html \
    successes.html \
    support.html \
    tips.html \
    tradeoffs.html \
    versionId.html \
    what-gets-installed.html.in \
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
    feedtypes/howto-nnexrad.html \
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
    LDM-registry.html \
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
    what-gets-installed.html \
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
    feedtypes/howto-nnexrad.html \
    feedtypes/index.html \
    feedtypes/nexrad-fonx.html \
    feedtypes/ngraph.html \
    feedtypes/ngrid.html \
    feedtypes/nimage.html \
    feedtypes/nogaps_coamps.html \
    feedtypes/ntext.html \
    feedtypes/public_bulletins.html \
    feedtypes/wsi.html
BUILT_SOURCES		= LDM-registry.html
DISTCLEANFILES		= LDM-registry.html

LDM-registry.html:	$(srcdir)/regpar-top.html ../../regutil/regpar.tab
	echo '<!-- Generated automatically.  Do not edit. -->' >$@.tmp
	cat $(srcdir)/regpar-top.html >>$@.tmp
	echo '<table align="center" border="2" cellpadding="6"' \
	    'cellspacing="0">' >>$@.tmp
	echo '<tr>' >>$@.tmp
	echo '<th>Parameter Pathname</th>' >>$@.tmp
	echo '<th>Parameter Description</th>' >>$@.tmp
	echo '<th>Default Value</th>' >>$@.tmp
	echo '</tr>' >>$@.tmp
	while IFS=: read name path desc default varName; do \
	    echo "<tr>"; \
		echo "<td nowrap><tt>$$path</tt></td>"; \
		echo "<td>$$desc</td>"; \
		echo "<td><tt>$$default</tt></td>"; \
	    echo "</tr>"; \
	done <../../regutil/regpar.tab >>$@.tmp
	echo '</blockquote></BODY></HTML>' >>$@.tmp
	mv $@.tmp $@

if MAINTAINER

all:	$(srcdir)/successes.html $(srcdir)/failures.html

configuring.html:	configuring.html.in
	$(top_srcdir)/regutil/substPaths <$< >$@.tmp
	mv $@.tmp $@

registry-check:	$(top_srcdir)/configure.ac
	sed -n '/UD_CONFIGPAR/{s/^.*(//;s/,.*//;s/\[//;s/\]//;p}' \
	    $(top_srcdir)/configure.ac \
	| while read name; do \
	    if ! grep -q $$name LDM-registry.html.in; then \
		echo 1>&2 "Parameter \"$$name\" is missing from" \
		    "file \"LDM-registry.html.in\""; \
		exit 1; \
	    fi; \
	done
#	while read name value; do \
#	    if ! grep -q $$name LDM-registry.html; then \
#		echo 1>&2 "Parameter \"$$name\" is missing from the " \
#		    "registry description"; \
#		exit 1; \
#	    fi; \
#	done <$(top_srcdir)/regutil/configDefaults.txt

.PHONY:	registry-check

successes.html:	$(top_srcdir)/test/results.tab
	sort -t , -k 2,2 -k 3,3 -k 6,6 -k 4,4 -k 7,7 $< \
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
		print "        <th>C Compiler</th>"; \
		print "        <th>Environment Variable(s)</th>"; \
		print "        <th>make(1)</th>"; \
		print "        <th>configure(1) Option</th>"; \
		print "      </tr>"; \
	    } \
	    $$8 != 0 { \
		print "      <tr>"; \
		print "        <td>" $$2 "</td>"; \
		print "        <td>" $$3 "</td>"; \
		print "        <td>" $$6 "</td>"; \
		print "        <td>" $$4 "</td>"; \
		print "        <td>" $$5 "</td>"; \
		print "        <td>" $$7 "</td>"; \
		print "      </tr>"; \
	    } \
	    END { \
		print "    </table>"; \
		print "  </body>"; \
		print "</html>"; \
	    }' >$@

failures.html:	$(top_srcdir)/test/results.tab
	sort -t , -k 2,2 -k 3,3 -k 6,6 -k 4,4 -k 7,7 $< \
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
		print "        <th>C Compiler</th>"; \
		print "        <th>Environment Variable(s)</th>"; \
		print "        <th>make(1)</th>"; \
		print "        <th>configure(1) Option</th>"; \
		print "      </tr>"; \
	    } \
	    $$8 == 0 { \
		print "      <tr>"; \
		print "        <td>" $$2 "</td>"; \
		print "        <td>" $$3 "</td>"; \
		print "        <td>" $$6 "</td>"; \
		print "        <td>" $$4 "</td>"; \
		print "        <td>" $$5 "</td>"; \
		print "        <td>" $$7 "</td>"; \
		print "      </tr>"; \
	    } \
	    END { \
		print "    </table>"; \
		print "  </body>"; \
		print "</html>"; \
	    }' >$@

WEB_DESTDIR	= /web/content/software/ldm/$(PACKAGE)-$(VERSION)

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    *.gif \
	    feedtypes \
	    $(WEB_DESTDIR)/basics

endif
