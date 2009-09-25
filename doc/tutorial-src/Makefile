include ../../macros.make

all:	web-update

clean:
	rm -rf *.Z ../tutorial

web-update:	../tutorial
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    ../tutorial/ \
	    conan:/content/software/ldm/$(PACKAGE)-$(BUG_VERSION)/tutorial

../tutorial:	FORCE
	rm -rf $@
	tclsh ~/java/metapps/auxdata/docs/build/generate.tcl -verbose \
	    -target $@ \
	    -noglossary \
	    -notclevaluation

FORCE:
