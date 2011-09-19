# Makefile for "gribinsert"

EXTRA_DIST		= etc

bin_PROGRAMS		= gribinsert

gribinsert_SOURCES	= \
    gribinsert.c gribinsert.h \
    get_grib_info.c \
    get_gribname.c \
    gribid.c \
    wgrib.c

CPPFLAGS		= \
	-I$(top_srcdir)/grib2 \
	-I$(top_srcdir)/gempak \
	-I$(top_srcdir)/ulog \
	-I$(top_srcdir)/protocol \
	-I$(top_srcdir)/pq \
	-I$(top_srcdir)/misc \
	-I$(top_srcdir) \
	@CPPFLAGS@

LDADD 			= \
      $(top_builddir)/lib/libldm.la $(top_builddir)/libxml2/libxml2.la

check:			$(bin_PROGRAMS)
	test -f /tmp/test.pq || pqcreate -s 2m /tmp/test.pq
	./$(bin_PROGRAMS) -f CONDUIT -q /tmp/test.pq -v -l - \
	    testdata/fh.0000_tl.press_gr.0p5deg
	cd .. && gribinsert/$(bin_PROGRAMS) -f CONDUIT -q /tmp/test.pq -v -l - \
	    gribinsert/testdata/fh.0000_tl.press_gr.awp218
	rm /tmp/test.pq

.c.i:
	$(COMPILE) -E $< >$@
