# Copyright 2011 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in the
# top-level source-directory of the package for copying and redistribution
# conditions.
#
## Process this file with automake(1) to produce file Makefile.in

################################################################################

LDMHOME			= @LDMHOME@
LDMSRC			= $(top_srcdir)
SUDO			= @SUDO@
SU			= @SU@
docdir			= @docdir@/noaaport

EXTRA_DIST 		= \
	Doxyfile \
	extractDecls \
	fifo.hin \
	fileReader.hin \
	getFacilityName.hin \
	html \
	ldmProductQueue.hin \
	mainpage.h \
	multicastReader.hin \
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
DISTCLEANFILES		= $(BUILT_SOURCES)

CPPFLAGS		= \
	-I$(top_srcdir) \
	-I$(top_srcdir)/grib2 \
	-I$(top_srcdir)/gempak \
	-I$(top_srcdir)/ulog \
	-I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
	-I$(top_builddir)/registry -I$(top_srcdir)/registry \
	-I$(top_srcdir)/pq \
	-I$(top_srcdir)/misc \
	-I$(srcdir)/retrans \
	@CPPFLAGS@ 

if WANT_RETRANS
    DEFS		+= -DRETRANS_SUPPORT
endif 

bin_PROGRAMS		= dvbs_multicast readnoaaport noaaportIngester
dist_bin_SCRIPTS	= \
	dvbs_goes \
	dvbs_nwstg \
	dvbs_nwstg2 \
	dvbs_oconus \
	nplog_rotate

COMMON_SOURCES		= \
	dvbs.h \
	grib2name.c \
	gribid.c \
	ldmProductQueue.c ldmProductQueue.h \
	nport.h \
	png_io.c \
	process_prod.c \
	readpdb.c \
	readpdh.c \
	readpsh.c\
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
	multicastReader.c multicastReader.h \
	productMaker.c productMaker.h \
	reader.c reader.h

LDADD 			= $(top_builddir)/lib/libldm.la

TAGS_FILES		= \
	*.c *.h \
	../grib2/*.c ../grib2/*.h \
	../gempak/*.c ../gempak/*.h \
	../pq/*.c ../pq/*.h \
	../protocol/*.c ../protocol/*.h \
	../registry/*.c ../registry/*.h \
	../ulog/*.c ../ulog/*.h \
	../misc/*.c ../misc/*.h \
	../registry/*.c ../registry/*.h \
	../rpc/*.c ../rpc/*.h
if WANT_RETRANS
    TAGS_FILES		+= retrans/*.c retrans/*.h
endif

$(srcdir)/html \
$(srcdir)/html/index.html:	$(srcdir)/mainpage.h $(srcdir)/Doxyfile
	cd $(srcdir) && doxygen Doxyfile

.hin.h:
	./extractDecls $*.hin $*.c >$@.tmp
	mv -f $@.tmp $@
shmfifo.h:		shmfifo.c shmfifo.hin extractDecls
ldmProductQueue.h:	ldmProductQueue.c ldmProductQueue.hin extractDecls
fifo.h:			fifo.c fifo.hin extractDecls
reader.h:		reader.c reader.hin extractDecls
fileReader.h:		fileReader.c fileReader.hin extractDecls
multicastReader.h:	multicastReader.c multicastReader.hin extractDecls
productMaker.h:		productMaker.c productMaker.hin extractDecls
getFacilityName.h:	getFacilityName.c getFacilityName.hin extractDecls

.c.i:
	$(COMPILE) -E $< >$@

installcheck-local:
	$(DESTDIR)$(bindir)/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	$(DESTDIR)$(bindir)/readnoaaport -l- -q /tmp/readnoaaport-test.pq \
	    $(srcdir)/nwstgdump.data
	rm /tmp/readnoaaport-test.pq

check-local:		check-readnoaaport check-noaaportIngester
check-readnoaaport:	readnoaaport
	../pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	./readnoaaport -nl- -q /tmp/readnoaaport-test.pq \
	    $(srcdir)/nwstgdump.data
	rm /tmp/readnoaaport-test.pq
check-noaaportIngester:	noaaportIngester
	../pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	./noaaportIngester -b 3 -q /tmp/noaaportIngester-test.pq \
	    <$(srcdir)/nwstgdump.data
	rm /tmp/noaaportIngester-test.pq

valgrind:		valgrind-readnoaaport valgrind-noaaportIngester

valgrind-readnoaaport:		readnoaaport
	../pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes \
	    readnoaaport -l- -q /tmp/readnoaaport-test.pq nwstgdump.data
	rm /tmp/readnoaaport-test.pq

sudo:
if HAVE_SUDO
	$(SUDO) $(MAKE) $(AM_MAKEFLAGS) $(TARGET) 2>/dev/tty
else
if HAVE_SU
	@printf "Enter root's password (or don't): " >/dev/tty
	@$(SU) root -c 'PATH='$$PATH' $(MAKE) $(AM_MAKEFLAGS) $(TARGET)' \
	    </dev/tty 2>/dev/tty
	@echo >/dev/tty
endif
endif
	
valgrind-noaaportIngester:	noaaportIngester
	../pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	$(MAKE) $(AM_MAKEFLAGS) sudo TARGET=root-ni-vg
	rm /tmp/noaaportIngester-test.pq

root-ni-vg:
	$(LIBTOOL) --mode=execute valgrind --leak-check=yes \
	    noaaportIngester -n -q /tmp/noaaportIngester-test.pq \
		<$(srcdir)/nwstgdump.data

debug-readnoaaport:	readnoaaport
	../pqcreate/pqcreate -c -s 2m /tmp/readnoaaport-test.pq
	echo 'handle SIGCONT pass noprint nostop' >/tmp/readnoaaport.gdb
	echo 'b 617' >>/tmp/readnoaaport.gdb
	echo 'run -l- -q /tmp/readnoaaport-test.pq nwstgdump.data' \
	    >>/tmp/readnoaaport.gdb
	$(LIBTOOL) --mode=execute gdb -x /tmp/readnoaaport.gdb readnoaaport
	rm /tmp/readnoaaport-test.pq /tmp/readnoaaport.gdb

debug-noaaportIngester:	noaaportIngester
	../pqcreate/pqcreate -c -s 2m /tmp/noaaportIngester-test.pq
	echo 'handle SIGCONT pass noprint nostop' >/tmp/noaaportIngester.gdb
	echo 'handle SIGTERM pass nostop' >>/tmp/noaaportIngester.gdb
	echo 'run -q /tmp/noaaportIngester-test.pq -n -m 224.0.1.1' \
	    >>/tmp/noaaportIngester.gdb
	$(MAKE) $(AM_MAKEFLAGS) sudo TARGET=root-ni-db
	rm /tmp/noaaportIngester-test.pq /tmp/noaaportIngester.gdb

root-ni-db:
	$(LIBTOOL) --mode=execute gdb -x /tmp/noaaportIngester.gdb \
	      noaaportIngester

install-html:		$(srcdir)/html/index.html $(DESTDIR)$(docdir)
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
