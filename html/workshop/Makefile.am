htmldir = ${docdir}/workshop
html_DATA	= \
    evaluation.html \
    handout.html \
    index.html \
    specifics.html

DISTCLEANFILES	= specifics.html

EXTRA_DIST	= $(html_DATA)

WEB_DESTDIR	= /web/content/software/ldm/$(PACKAGE)-$(VERSION)

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    $(WEB_DESTDIR)/workshop
