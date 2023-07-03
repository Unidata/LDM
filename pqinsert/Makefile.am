# Copyright 2015 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST 	= pq_test_insert.txt
CLEANFILES      = *.pq *.log
PQ_SUBDIR	= @PQ_SUBDIR@

bin_PROGRAMS	= pqinsert pq_test_insert
AM_CPPFLAGS	= \
    -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir) \
    -I$(top_srcdir)/mcast_lib/ldm7
pqinsert_LDADD		= $(top_builddir)/lib/libldm.la
pq_test_insert_LDADD	= $(pqinsert_LDADD)
TAGS_FILES	= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../log/*.c ../log/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h
dist_man1_MANS	= pqinsert.1

check-local:		pq_test_insert
# The "-c" (clobber) option is used to prevent `pq_test_insert` from exiting due
# to a duplicate data-product from a previous invocation.
	../pqcreate/pqcreate -c -s 2M -q pq_test_insert.pq
#	cat $(srcdir)/pq_test_insert.txt
	./pq_test_insert -vl - -q pq_test_insert.pq $(srcdir)/pq_test_insert.txt
#	1000 products of mean size 1000 kB inserted as quickly as possible into
#	queue that's limited equally by data and slot capacities
	../pqcreate/pqcreate -c -s 100k -S 100 -q pq_test_insert.pq
	./pq_test_insert -q pq_test_insert.pq -m 2000 -n 1000 -g 0
	rm pq_test_insert.pq

valgrind:		pq_test_insert
# The "-c" (clobber) option is used to prevent `pq_test_insert` from exiting due
# to a duplicate data-product from a previous invocation.
	$(top_builddir)/pqcreate/pqcreate -c -s 500k -q pq_test_insert.pq
	rm -f pq_test_insert.log 
	cat $(srcdir)/pq_test_insert.txt
	$(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./pq_test_insert -vl - \
	    -q pq_test_insert.pq #$(srcdir)/pq_test_insert.txt
