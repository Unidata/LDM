htmldir	= $(docdir)
EXTRA_DIST		= \
    tutorial/activate.html \
    tutorial/configure.html \
    tutorial/default.css \
    tutorial/frameleft.html \
    tutorial/frames.html \
    tutorial/fulltoc.html \
    tutorial/help.html \
    tutorial/imageindex.html \
    tutorial/index.html \
    tutorial/install.html \
    tutorial/introduction.html \
    tutorial/mainindex.html \
    tutorial/monitor.html \
    tutorial/stylesheet.css \
    tutorial/toc.html \
    tutorial/understand.html \
    tutorial/whatis.html \
    tutorial/images/blankdart.gif \
    tutorial/images/blueline.gif \
    tutorial/images/dart.gif \
    tutorial/images/Frames.gif \
    tutorial/images/Glossary.gif \
    tutorial/images/Help16.gif \
    tutorial/images/Images.gif \
    tutorial/images/Index.gif \
    tutorial/images/NextArrowDisabled.gif \
    tutorial/images/NextArrow.gif \
    tutorial/images/Play16.gif \
    tutorial/images/PreviousArrowDisabled.gif \
    tutorial/images/PreviousArrow.gif \
    tutorial/images/Schedule.gif \
    tutorial/images/TOCIcon.gif \
    tutorial/images/TopArrow.gif
nobase_html_DATA	= \
    tutorial/activate.html \
    tutorial/configure.html \
    tutorial/default.css \
    tutorial/frameleft.html \
    tutorial/frames.html \
    tutorial/fulltoc.html \
    tutorial/help.html \
    tutorial/imageindex.html \
    tutorial/index.html \
    tutorial/install.html \
    tutorial/introduction.html \
    tutorial/mainindex.html \
    tutorial/monitor.html \
    tutorial/stylesheet.css \
    tutorial/toc.html \
    tutorial/understand.html \
    tutorial/whatis.html \
    tutorial/images/blankdart.gif \
    tutorial/images/blueline.gif \
    tutorial/images/dart.gif \
    tutorial/images/Frames.gif \
    tutorial/images/Glossary.gif \
    tutorial/images/Help16.gif \
    tutorial/images/Images.gif \
    tutorial/images/Index.gif \
    tutorial/images/NextArrowDisabled.gif \
    tutorial/images/NextArrow.gif \
    tutorial/images/Play16.gif \
    tutorial/images/PreviousArrowDisabled.gif \
    tutorial/images/PreviousArrow.gif \
    tutorial/images/Schedule.gif \
    tutorial/images/TOCIcon.gif \
    tutorial/images/TopArrow.gif

if MAINTAINER
all:		$(EXTRA_DIST)

$(EXTRA_DIST):
	rm -rf tutorial
	tclsh ~/java/metapps/auxdata/docs/build/generate.tcl -verbose \
	    -target tutorial \
	    -noglossary \
	    -notclevaluation

WEB_DESTDIR	= /web/content/software/ldm/$(PACKAGE)-$(VERSION)

web-update:	$(EXTRA_DIST)
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    tutorial/ \
	    $(WEB_DESTDIR)/tutorial

clean-local:
	rm -rf tutorial

.PHONY:		web-update tutorial
endif
