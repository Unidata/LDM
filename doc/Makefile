include macros.make

all:	commit-check web-update

clean:
	rm -rf *.Z

commit-check:	FORCE
	if cvs status 2>/dev/null | grep Status: \
		| grep -v Up-to-date >/dev/null; then \
	    echo 1>&2 'Package needs "cvs commit"'; \
	    exit 1; \
	else true; fi

web-update:	FORCE
	rsync $(RSYNC_FLAGS) -u \
	    history.html \
	    factsheet.html \
	    generalHelp.html \
	    conan:/content/software/ldm
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    README \
	    CHANGE_LOG \
	    *.html \
	    install \
	    troubleshooting \
	    conan:/content/software/ldm/$(PACKAGE)-$(BUG_VERSION)
	cd basics && make $@
	cd tutorial-src && make $@
	cd workshop && make $@

$(PACKAGE)-$(REV_VERSION)-doc.tar.Z:	web-update
	ssh conan 'pax -x ustar -w -s \
	    ";/content/software/ldm/$(PACKAGE)-$(BUG_VERSION);doc;" \
	    /content/software/ldm/$(PACKAGE)-$(BUG_VERSION)' | compress >$@

FORCE:
