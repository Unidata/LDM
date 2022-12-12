htmldir = ${docdir}/workshop
html_DATA	= \
    evaluation.html \
    handout.html \
    index.html \
    specifics.html

DISTCLEANFILES	= specifics.html

EXTRA_DIST	= $(html_DATA)

WEBHOST		= docs.unidata.ucar.edu
WEB_DESTDIR	= /web/docs/ldm/$(VERSION)

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    $(WEBHOST):$(WEB_DESTDIR)/workshop
