include ../../macros.make

HTMLS	=	\
	ERE.html	\
	LDM-user.html	\
	activating.html	\
	become_ldm.html	\
	behavior.html	\
	configuring.html	\
	data-product.html	\
	generic-LDM.html	\
	glindex.html	\
	huge-queues.html	\
	index.html	\
	ldmd.conf.html	\
	logfile-format.html	\
	logfile.html	\
	monitoring.html	\
	newlog.html	\
	platform.html	\
	pqact.conf.html	\
	preinstallation.html	\
	product-queue.html	\
	programs.html	\
	protocol.html	\
	scour.conf.html	\
	source-install-steps.html	\
	source_or_binary.html	\
	startStop.html	\
	support.html	\
	tradeoffs.html	\
	vet-ldmadmin.html	\
	feedtypes/conduit.html	\
	feedtypes/fnexrad.html	\
	feedtypes/gem.html	\
	feedtypes/hds.html	\
	feedtypes/index.html	\
	feedtypes/nexrad-fonx.html	\
	feedtypes/ngraph.html	\
	feedtypes/ngrid.html	\
	feedtypes/nimage.html	\
	feedtypes/nogaps_coamps.html	\
	feedtypes/ntext.html	\
	feedtypes/public_bulletins.html	\
	feedtypes/wsi.html
GIFS	=	\
	downstream6.gif	\
	generic-LDM.gif	\
	upstream6.gif

all:	web-update

clean:
	rm -rf *.Z *.zip

web-update:
	rsync $(RSYNC_FLAGS) -aCu --delete --delete-excluded \
	    *.html \
	    *.gif \
	    feedtypes \
	    conan:/content/software/ldm/ldm-$(BUG_VERSION)/basics

FORCE:
