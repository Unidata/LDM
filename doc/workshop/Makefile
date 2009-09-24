include ../macros.make

all:	web-update

clean:
	rm -rf *.Z

web-update:
	rsync --rsh=ssh -aCu --delete \
	    --rsync-path=/opt/bin/rsync $(RSYNC_FLAGS) \
	    *.html \
	    conan:/content/software/ldm/$(PACKAGE)-$(BUG_VERSION)/workshop

FORCE:
