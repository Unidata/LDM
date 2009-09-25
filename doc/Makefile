include ../macros.make

all:
	cd tutorial && $(MAKE) $@

clean:
	rm -rf *.Z
	cd tutorial && $(MAKE) $@

commit-check:	FORCE
	if git status -a >/dev/null; then \
	    echo 1>&2 'Package needs "git commit -a"'; \
	    exit 1; \
	fi

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
	    conan:/content/software/ldm/$(PACKAGE)-$(BUG_VERSION)
	cd basics && make $@
	cd tutorial && make $@
	cd workshop && make $@

FORCE:
