include ../../macros.make

all:
	rm -rf tutorial
	tclsh ~/java/metapps/auxdata/docs/build/generate.tcl -verbose \
	    -target tutorial \
	    -noglossary \
	    -notclevaluation

clean:
	rm -rf *.Z tutorial

web-update:	tutorial
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    tutorial/ \
	    conan:/content/software/ldm/$(PACKAGE)-$(BUG_VERSION)/tutorial

FORCE:
