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
SUDO                    = @SUDO@
SU                      = @SU@
docdir                  = @DOCDIR_UTILITIES@/noaaport
DOXYGEN                 = @DOXYGEN@

EXTRA_DIST              = \
	Doxyfile \
	extractDecls \
	fifo.hin \
	fileReader.hin \
	getFacilityName.hin \
	html \
	ldmProductQueue.hin \
        mainpage.h \
        mrms1.data \
        multicastReader.hin \
        readnoaaport.slog \
        readnoaaport.ulog \
        noaaportIngester.slog \
        noaaportIngester.ulog \
        noaaportIngester-nssl.slog \
        noaaportIngester-nssl.ulog \
        nwstgdump.data \
        productMaker.hin \
        reader.hin \
	shmfifo.hin

BUILT_SOURCES		= \
	fifo.h \
	fileReader.h \
	getFacilityName.h \
	ldmProductQueue.h \
	multicastReader.h \
	productMaker.h \
	reader.h \
	shmfifo.h

DISTCLEANFILES          = $(BUILT_SOURCES)
CLEANFILES              = $(BUILT_SOURCES) *.out *.log

AM_CPPFLAGS             = \
        -I$(top_srcdir) \
        -I$(top_srcdir)/grib2 \
        -I$(top_srcdir)/gempak \
        -I$(top_srcdir)/zlib \
        -I$(top_srcdir)/log \
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

bin_PROGRAMS		= dvbs_multicast readnoaaport noaaportIngester
dist_bin_SCRIPTS	= \
	dvbs_goes \
	dvbs_nwstg \
	dvbs_nwstg2 \
	dvbs_oconus \
	nplog_rotate

COMMON_SOURCES          = \
        decode.h \
        dvbs.h \
        frame_queue.c frame_queue.h \
        grib2name.c \
        ldmProductQueue.c ldmProductQueue.h \
        nbs_transport.c nbs_transport.h \
        nbs_presentation.c nbs_presentation.h \
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
	fileReader.c fileReader.h \
	getFacilityName.c getFacilityName.h \
	lockProcessInMemory.c \
	multicastReader.c multicastReader.h \
	noaaport_socket.c noaaport_socket.h \
	productMaker.c productMaker.h \
	reader.c reader.h

LDADD 			= $(top_builddir)/lib/libldm.la -lxml2
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
        $(top_srcdir)/rpc/*.c $(top_srcdir)/rpc/*.h
if WANT_RETRANS
    TAGS_FILES		+= retrans/*.c retrans/*.h
endif

$(srcdir)/html \
$(srcdir)/html/index.html:	$(srcdir)/mainpage.h $(srcdir)/Doxyfile
	cd $(srcdir) && $(DOXYGEN) Doxyfile

.hin.h:
	./extractDecls $*.hin $*.c >$@.tmp
	mv -f $@.tmp $@
shmfifo.h:		shmfifo.c shmfifo.hin extractDecls
ldmProductQueue.h:	ldmProductQueue.c ldmProductQueue.hin extractDecls
fifo.h:			fifo.c fifo.hin extractDecls
reader.h:		reader.c reader.hin extractDecls
fileReader.h:		fileReader.c fileReader.hin extractDecls
getFacilityName.h:	getFacilityName.c getFacilityName.hin extractDecls
Grib2Decoder.h:		Grib2Decoder.c Grib2Decoder.hin extractDecls
ldmProductQueue.h:	ldmProductQueue.c ldmProductQueue.hin extractDecls
multicastReader.h:	multicastReader.c multicastReader.hin extractDecls
productMaker.h:		productMaker.c productMaker.hin extractDecls

.c.i:
	$(COMPILE) -E $< >$@

if WANT_SLOG
READNOAAPORT_LOG                = readnoaaport.slog
NOAAPORTINGESTER_LOG            = noaaportIngester.slog
NOAAPORTINGESTER_NSSL_LOG       = noaaportIngester-nssl.slog
CUT_FIELD                       = 5
endif
if WANT_ULOG
READNOAAPORT_LOG                = readnoaaport.ulog
NOAAPORTINGESTER_LOG            = noaaportIngester.ulog
NOAAPORTINGESTER_NSSL_LOG       = noaaportIngester-nssl.ulog
CUT_FIELD                       = 5
endif

installcheck-local:
	$(DESTDIR)$(bindir)/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	$(DESTDIR)$(bindir)/readnoaaport -l- -q /tmp/readnoaaport-test.pq \
	    $(srcdir)/nwstgdump.data
	rm /tmp/readnoaaport-test.pq

check-local:            check-readnoaaport check-noaaportIngester \
                        check-noaaportIngester-nssl
check-readnoaaport:     readnoaaport $(srcdir)/$(READNOAAPORT_LOG)
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	LDM_REGISTRY_DIR=../regutil ./readnoaaport -nl- \
	    -q /tmp/readnoaaport-test.pq \
	    $(srcdir)/nwstgdump.data >readnoaaport.out 2>&1
	rm /tmp/readnoaaport-test.pq
	fgrep 'process_prod()' readnoaaport.out | cut -d ' ' -f $(CUT_FIELD)- \
		>readnoaaport.log
	diff readnoaaport.log $(srcdir)/$(READNOAAPORT_LOG)

noaaportIngester.out:           noaaportIngester
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	LDM_REGISTRY_DIR=../regutil ./noaaportIngester -l- -b 2 -n \
	    -q /tmp/noaaportIngester-test.pq \
	    <$(srcdir)/nwstgdump.data >$@ 2>&1
	rm /tmp/noaaportIngester-test.pq
noaaportIngester.log:           noaaportIngester.out
	fgrep 'process_prod()' <$? | cut -d ' ' -f $(CUT_FIELD)- >$@
check-noaaportIngester:         noaaportIngester.log \
	                        $(srcdir)/$(NOAAPORTINGESTER_LOG)
	diff noaaportIngester.log $(srcdir)/$(NOAAPORTINGESTER_LOG)

noaaportIngester-nssl.out:      noaaportIngester
	-rm /tmp/noaaportIngester-test.pq wmocenter.tbl g2varsnssl1.tbl \
	    g2vcrdwmo255.tbl
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	ln -s $(top_srcdir)/gempak/tables/wmocenter.tbl .
	ln -s $(top_srcdir)/gempak/tables/g2varsnssl.tbl g2varsnssl1.tbl
	ln -s $(top_srcdir)/gempak/tables/g2vcrdwmo.tbl g2vcrdwmo255.tbl
	LDM_REGISTRY_DIR=../regutil ./noaaportIngester -nl- \
	    -q /tmp/noaaportIngester-test.pq \
	    <$(srcdir)/mrms1.data >$@ 2>&1
	rm /tmp/noaaportIngester-test.pq wmocenter.tbl g2varsnssl1.tbl \
	    g2vcrdwmo255.tbl
noaaportIngester-nssl.log:      noaaportIngester-nssl.out
	fgrep 'process_prod()' <$? | cut -d ' ' -f $(CUT_FIELD)- >$@
check-noaaportIngester-nssl:    noaaportIngester-nssl.log \
				$(srcdir)/$(NOAAPORTINGESTER_NSSL_LOG)
	diff noaaportIngester-nssl.log $(srcdir)/$(NOAAPORTINGESTER_NSSL_LOG)

valgrind:               valgrind-readnoaaport valgrind-noaaportIngester

valgrind-readnoaaport:		readnoaaport
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes \
	    readnoaaport -l- -q /tmp/readnoaaport-test.pq nwstgdump.data
	rm /tmp/readnoaaport-test.pq

if HAVE_CUNIT
check_PROGRAMS			= frame_queue_test
frame_queue_test_SOURCES	= frame_queue_test.c \
				  frame_queue.c frame_queue.h
frame_queue_test_LDADD		= $(LDADD) @LIBS_CUNIT@
TESTS				= frame_queue_test
valgrind-frame_queue:	frame_queue_test
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes frame_queue_test
endif

sudo:
if HAVE_SUDO
	$(SUDO) $(MAKE) $(AM_MAKEFLAGS) $(TARGET) 2>/dev/tty
else
	@printf "Enter root's password (or don't): " >/dev/tty
	@su root -c 'PATH='$$PATH' $(MAKE) $(AM_MAKEFLAGS) $(TARGET)' \
	    </dev/tty 2>/dev/tty
	@echo >/dev/tty
endif

valgrind-noaaportIngester:      noaaportIngester
	$(top_builddir)/pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	LDM_REGISTRY_DIR=../regutil $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=yes .libs/noaaportIngester -b 1 -q \
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
