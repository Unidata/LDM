# Copyright 2011 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in the
# top-level source-directory of the package for copying and redistribution
# conditions.
#
## Process this file with automake(1) to produce file Makefile.in

################################################################################

LDMHOME			= @LDMHOME@
LDMSRC                  = $(top_srcdir)
docdir                  = @DOCDIR_UTILITIES@/noaaport
DOXYGEN                 = @DOXYGEN@

EXTRA_DIST              = \
	Doxyfile \
	html \
        mainpage.h \
        mrms1.data \
        readnoaaport.slog \
        readnoaaport.ulog \
        noaaportIngester.slog \
        noaaportIngester.ulog \
        noaaportIngester-nssl.slog \
        noaaportIngester-nssl.ulog \
        nwstgdump.data \
	SUPER-NATIONAL_8km_IR_20160422_1915.gini

DISTCLEANFILES          = $(BUILT_SOURCES)
CLEANFILES              = $(BUILT_SOURCES) *.out *.log *.pq output.gini blender.1 

AM_CPPFLAGS             = \
        -I$(top_srcdir) \
        -I$(top_srcdir)/grib2 \
        -I$(top_srcdir)/gempak \
        -I$(top_srcdir)/zlib \
        -I$(top_builddir)/log      -I$(top_srcdir)/log \
        -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
        -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
        -I$(top_builddir)/registry -I$(top_srcdir)/registry \
        -I$(top_srcdir)/protocol \
        -I$(top_srcdir)/pq \
        -I$(top_srcdir)/misc \
        -I$(srcdir)/retrans

if WANT_RETRANS
    DEFS                += -DRETRANS_SUPPORT
endif 

bin_PROGRAMS		= dvbs_multicast readnoaaport noaaportIngester \
                          nbs_ingest blender testBlender  
dist_bin_SCRIPTS	= \
	dvbs_goes \
	dvbs_nwstg \
	dvbs_nwstg2 \
	dvbs_oconus \
	nplog_rotate \
	noaaportBlender.py
	
nbs_ingest_SOURCES	= nbs_ingest.c \
                          nbs.h \
                          nbs_link_filedes.c nbs_link.h \
                          nbs_transport.c nbs_transport.h \
                          nbs_presentation.c nbs_presentation.h \
                          nbs_application.c nbs_application.h \
                          nbs_stack.c nbs_stack.h \
                          noaaport_socket.c noaaport_socket.h \
                          decode.h \
                          gini.c gini.h \
                          dynabuf.c dynabuf.h

COMMON_SOURCES          = \
        decode.h \
        dvbs.h \
        dynabuf.c dynabuf.h \
        gini.c gini.h \
        grib2name.c \
        ldmProductQueue.c ldmProductQueue.h \
        nport.h \
	png_io.c \
        process_prod.c \
        readpdb.c \
        readpdh.c \
        readpsh.c \
        readsbn.c \
        redbook_header.c \
        wgrib.c \
	wmo_header.c \
	wmo_header.h
SHMFIFO_SOURCES		= shmfifo.c shmfifo.h

dvbs_multicast_SOURCES	= dvbs_multicast.c $(SHMFIFO_SOURCES) $(COMMON_SOURCES)

readnoaaport_SOURCES	= readnoaaport.c $(SHMFIFO_SOURCES) $(COMMON_SOURCES)

noaaportIngester_SOURCES	= noaaportIngester.c $(COMMON_SOURCES) \
	fifo.c fifo.h \
	getFacilityName.c getFacilityName.h \
	goes.h \
	lockProcessInMemory.c \
	noaaport_socket.c noaaport_socket.h \
	productMaker.c productMaker.h \
	reader.c reader.h

blender_SOURCES		= blender.c blender.h $(COMMON_SOURCES) \
	frameWriter.c frameWriter.h noaaportFrame.h queueManager.c \
	queueManager.h frameReader.c frameReader.h FrameQueue.cpp \
	FrameQueue.h misc.c NbsHeaders.c NbsHeaders.h NbsFrame2.c NbsFrame.h
 	
testBlender_SOURCES	= testBlender.c  $(COMMON_SOURCES)

LDADD 		= $(top_builddir)/lib/libldm.la -lxml2
if WANT_RETRANS
  LDADD 	+= retrans/lib.la
endif

TAGS_FILES		= \
	*.c *.h \
	$(top_srcdir)/grib2/*.c $(top_srcdir)/grib2/*.h \
	$(top_srcdir)/gempak/*.c $(top_srcdir)/gempak/*.h \
	$(top_srcdir)/pq/*.c $(top_srcdir)/pq/*.h \
        $(top_srcdir)/protocol/*.c $(top_srcdir)/protocol/*.h \
        $(top_srcdir)/protocol2/*.c $(top_srcdir)/protocol2/*.h \
        $(top_srcdir)/registry/*.c $(top_srcdir)/registry/*.h \
        $(top_srcdir)/log/*.c $(top_srcdir)/log/*.h \
        $(top_srcdir)/misc/*.c $(top_srcdir)/misc/*.h \
        $(top_srcdir)/registry/*.c $(top_srcdir)/registry/*.h \
        $(top_srcdir)/rpc/*.c $(top_srcdir)/rpc/*.h \
        $(blender_SOURCES) ../log/*.c ../log/*.h \
        $(testBlender_SOURCES) ../log/*.c ../log/*.h 

if WANT_RETRANS
    TAGS_FILES		+= retrans/*.c retrans/*.h
endif

$(srcdir)/html \
$(srcdir)/html/index.html:	$(srcdir)/mainpage.h $(srcdir)/Doxyfile
	cd $(srcdir) && $(DOXYGEN) Doxyfile

.c.i:
	$(COMPILE) -E $< >$@

if WANT_SLOG
READNOAAPORT_LOG                = readnoaaport.slog
NOAAPORTINGESTER_LOG            = noaaportIngester.slog
NOAAPORTINGESTER_NSSL_LOG       = noaaportIngester-nssl.slog
CUT_FIELDS                      = 5-
endif
if WANT_ULOG
READNOAAPORT_LOG                = readnoaaport.ulog
NOAAPORTINGESTER_LOG            = noaaportIngester.ulog
NOAAPORTINGESTER_NSSL_LOG       = noaaportIngester-nssl.ulog
CUT_FIELDS                      = 5,7-
endif

installcheck-local:
	$(DESTDIR)$(bindir)/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	$(DESTDIR)$(bindir)/readnoaaport -l- -q /tmp/readnoaaport-test.pq \
	    $(srcdir)/nwstgdump.data
	rm /tmp/readnoaaport-test.pq

if HAVE_CUNIT
check-local: 		check-readnoaaport check-noaaportIngester \
                        check-noaaportIngester-nssl \
                        check-nbs_test
else
check-local:            check-readnoaaport check-noaaportIngester \
                        check-noaaportIngester-nssl
endif
check-readnoaaport:     readnoaaport $(srcdir)/$(READNOAAPORT_LOG)
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	LDM_REGISTRY_DIR=../regutil ./readnoaaport -nl- \
	    -q /tmp/readnoaaport-test.pq \
	    $(srcdir)/nwstgdump.data >readnoaaport.out 2>&1
	rm /tmp/readnoaaport-test.pq
	fgrep 'process_prod.c' readnoaaport.out | tr -s ' ' | cut -d ' ' -f $(CUT_FIELDS) \
		>readnoaaport.log
	if ! diff readnoaaport.log $(srcdir)/$(READNOAAPORT_LOG); then \
		head -n 44 readnoaaport.log; \
		exit 1; \
	fi

noaaportIngester.out:           noaaportIngester
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	LDM_REGISTRY_DIR=../regutil ./noaaportIngester -l- -b 2 -n \
	    -q /tmp/noaaportIngester-test.pq \
	    <$(srcdir)/nwstgdump.data >$@ 2>&1
	rm /tmp/noaaportIngester-test.pq
noaaportIngester.log:           noaaportIngester.out
	fgrep 'process_prod.c' <$? | tr -s ' ' | cut -d ' ' -f $(CUT_FIELDS) >$@
check-noaaportIngester:         noaaportIngester.log \
	                        $(srcdir)/$(NOAAPORTINGESTER_LOG)
	if ! diff noaaportIngester.log $(srcdir)/$(NOAAPORTINGESTER_LOG); then \
		head -n 44 noaaportIngester.log; \
		exit 1; \
	fi

noaaportIngester-nssl.out:      noaaportIngester
	rm -f /tmp/noaaportIngester-test.pq wmocenter.tbl g2varsnssl1.tbl \
	    g2vcrdnssl1.tbl g2vcrdwmo255.tbl
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	ln -s $(top_srcdir)/gempak/tables/wmocenter.tbl .
	ln -s $(top_srcdir)/gempak/tables/g2varsnssl.tbl g2varsnssl1.tbl
	ln -s $(top_srcdir)/gempak/tables/g2vcrdnssl.tbl g2vcrdnssl1.tbl
	ln -s $(top_srcdir)/gempak/tables/g2vcrdwmo.tbl g2vcrdwmo255.tbl
	LDM_REGISTRY_DIR=../regutil ./noaaportIngester -nl- \
	    -q /tmp/noaaportIngester-test.pq \
	    <$(srcdir)/mrms1.data >$@ 2>&1
	rm /tmp/noaaportIngester-test.pq wmocenter.tbl g2varsnssl1.tbl \
	    g2vcrdnssl1.tbl g2vcrdwmo255.tbl
noaaportIngester-nssl.log:      noaaportIngester-nssl.out
	fgrep 'process_prod.c' <$? | tr -s ' ' | cut -d ' ' -f $(CUT_FIELDS) >$@
check-noaaportIngester-nssl:    noaaportIngester-nssl.log \
				$(srcdir)/$(NOAAPORTINGESTER_NSSL_LOG)
	if test `uname -s` != Darwin; then \
	    if ! diff noaaportIngester-nssl.log $(srcdir)/$(NOAAPORTINGESTER_NSSL_LOG); then \
	        head -n 44 noaaportIngester-nssl.log; \
	        exit 1; \
	    fi; \
	fi

valgrind:               valgrind-readnoaaport valgrind-noaaportIngester

valgrind-blender:	blender	
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes \
	    blender -t 1.0 localhost:9127 localhost:9128

valgrind-readnoaaport:		readnoaaport
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes \
	    readnoaaport -l- -q /tmp/readnoaaport-test.pq nwstgdump.data
	rm /tmp/readnoaaport-test.pq

if HAVE_CUNIT
check_PROGRAMS			= nbs_test
#				  pipe_test socketpair_test frame_queue_test 

#pipe_test_SOURCES		= pipe_test.c
#pipe_test_LDADD		= $(LDADD) @LIBS_CUNIT@

#socketpair_test_SOURCES	= socketpair_test.c
#socketpair_test_LDADD		= $(LDADD) @LIBS_CUNIT@

#frame_queue_test_SOURCES	= frame_queue_test.c \
#				  frame_queue.c frame_queue.h
#frame_queue_test_LDADD		= $(LDADD) @LIBS_CUNIT@

nbs_test_SOURCES		= nbs_test.c \
				  nbs_application.c nbs_application.h \
				  nbs_presentation.c nbs_presentation.h \
				  nbs_transport.c nbs_transport.h \
				  nbs_link_filedes.c nbs_link.h \
				  nbs_stack.c nbs_stack.h \
				  decode.h \
				  gini.c gini.h \
				  dynabuf.c dynabuf.h
nbs_test_LDADD			= $(LDADD) @LIBS_CUNIT@

#TESTS				= pipe_test socketpair_test frame_queue_test 

check-nbs_test:	nbs_test
	./nbs_test $(srcdir)/SUPER-NATIONAL_8km_IR_20160422_1915.gini
	../pqcat/pqcat -l- -q nbs_test.pq >output.gini
	if cmp $(srcdir)/SUPER-NATIONAL_8km_IR_20160422_1915.gini output.gini; then \
            rm output.gini nbs_test.pq; \
	fi

valgrind-nbs_test:	nbs_test
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes nbs_test

#valgrind-frame_queue:	frame_queue_test
#	$(LIBTOOL) --mode=execute valgrind --leak-check=yes frame_queue_test
endif

sudo:
	sudo $(MAKE) $(AM_MAKEFLAGS) $(TARGET) 2>/dev/tty

valgrind-noaaportIngester:      .libs/lt-noaaportIngester
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	LDM_REGISTRY_DIR=../regutil \
            valgrind \
	    --leak-check=yes .libs/lt-noaaportIngester -b 1 -q \
	    /tmp/noaaportIngester-test.pq <$(srcdir)/nwstgdump.data
	rm /tmp/noaaportIngester-test.pq

root-ni-vg:
	LDM_REGISTRY_DIR=../regutil $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=yes noaaportIngester -n \
	    -q /tmp/noaaportIngester-test.pq <$(srcdir)/nwstgdump.data

debug-readnoaaport:     readnoaaport
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	echo 'handle SIGCONT pass noprint nostop' >/tmp/readnoaaport.gdb
	echo 'b 617' >>/tmp/readnoaaport.gdb
	echo 'run -l- -q /tmp/readnoaaport-test.pq nwstgdump.data' \
	    >>/tmp/readnoaaport.gdb
	$(LIBTOOL) --mode=execute gdb -x /tmp/readnoaaport.gdb readnoaaport
	rm /tmp/readnoaaport-test.pq /tmp/readnoaaport.gdb

debug-noaaportIngester:	noaaportIngester
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	echo 'handle SIGCONT pass noprint nostop' >/tmp/noaaportIngester.gdb
	echo 'handle SIGTERM pass nostop' >>/tmp/noaaportIngester.gdb
	echo 'run -q /tmp/noaaportIngester-test.pq -n \
	    <$(srcdir)/nwstgdump.data' >>/tmp/noaaportIngester.gdb
	$(MAKE) $(AM_MAKEFLAGS) sudo TARGET=root-ni-db
	rm /tmp/noaaportIngester-test.pq /tmp/noaaportIngester.gdb

root-ni-db:
	LDM_REGISTRY_DIR=../regutil $(LIBTOOL) --mode=execute gdb -x \
	    /tmp/noaaportIngester.gdb noaaportIngester

install-html:           $(srcdir)/html/index.html $(DESTDIR)$(docdir)
	cp -R $(srcdir)/html/* $(DESTDIR)$(docdir)

$(DESTDIR)$(docdir):
	mkdir -p $@

install-data-hook:	install-html

uninstall-hook:
	chmod -R +w $(DESTDIR)$(docdir)
	rm -rf $(DESTDIR)$(docdir)

if MAINTAINER
WEB_DESTDIR	= /web/content/software/ldm/$(PACKAGE)-$(VERSION)/noaaport

commit-check:	Makefile
	if git status -u | fgrep -Fq 'nothing to commit'; then \
	    : true; \
	else \
	    echo 1>&2 'Package needs "git commit -u"'; \
	    exit 1; \
	fi

$(WEB_DESTDIR):
	mkdir -p $@

web-update:	$(WEB_DESTDIR)
	cp -R html/* $(WEB_DESTDIR)
endif

.PHONY:	\
	install-html \
	root-ni-ck \
	root-ni-db \
	root-ni-vg \
	sudo
