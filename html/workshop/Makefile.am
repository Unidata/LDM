htmldir = ${docdir}/workshop
html_DATA	= \
    evaluation.html \
    handout.html \
    index.html \
    specifics.html

EXTRA_DIST	= $(html_DATA)

if MAINTAINER

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    conan:/content/software/ldm/$(PACKAGE)-$(VERSION)/workshop

endif
