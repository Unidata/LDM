# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

CLEANFILES		= ldmd.1 *.i .*.info up7.conf down7.conf *.pq \
			  *.log *.pid *.yaml *.map *.pim
LD_GEN			= @LD_GEN@
EXTRA_DIST		= ldmd.1.in \
                          ldmd.conf \
			  nexrad3.txt
PQ_SUBDIR		= @PQ_SUBDIR@
bin_PROGRAMS	= ldmd
ldmd_SOURCES	= ldmd.c
AM_CPPFLAGS		= \
    -I$(top_srcdir)/registry \
    -I$(top_builddir)/registry \
    -I$(top_srcdir)/log \
    -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
    -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
    -I$(top_builddir)/registry -I$(top_srcdir)/registry \
    -I$(top_srcdir)/pq \
    -I$(top_srcdir)/misc \
    -I$(top_srcdir)
#    -I$(top_srcdir)/mcast_lib \
#    -I$(top_builddir)/mcast_lib/ldm7 -I$(top_srcdir)/mcast_lib/ldm7
LDADD		= \
    $(top_builddir)/lib/libldm.la \
    $(LD_GEN)
nodist_man1_MANS	= ldmd.1
TAGS_FILES		= \
    ../$(PQ_SUBDIR)/*.c ../$(PQ_SUBDIR)/*.h \
    ../protocol/*.c ../protocol/*.h \
    ../protocol2/*.c ../protocol2/*.h \
    ../registry/*.c ../registry/*.h \
    ../log/*.c ../log/*.h \
    ../misc/*.c ../misc/*.h \
    ../rpc/*.c ../rpc/*.h

ldmd.1:		$(srcdir)/ldmd.1.in
	../regutil/substPaths <$? >$@.tmp
	mv $@.tmp $@

SUFFIXES:	.h .hin
.hin.h:
	$(top_srcdir)/extractDecls $(srcdir)/$*.hin $(srcdir)/$*.c >$@.tmp
	mv -f $@.tmp $@

install-data-local:    \
    $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/ldmd.conf
$(DESTDIR)$(sysconfdir)/ldmd.conf:
	$(INSTALL_DATA) $(srcdir)/ldmd.conf $@

$(DESTDIR)$(sysconfdir):
	mkdir $@
	
TESTS_ENVIRONMENTS	= LDM_REGISTRY_DIR=../regutil PATH=../mldm_sender:$$PATH 

valgrind:	ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --suppressions=ldmd.supp --gen-suppressions=all --leak-check=full \
	    --show-reachable=yes --trace-children=no \
	    --child-silent-after-fork=no ./ldmd -l-

## The following doesn't work. If "--trace-children=no", then valgrind won't
## examine the LDM and if "--trace-children=yes", then valgrind won't execute
## the installed LDM because it's setuid and owned by root.
#installcheck-local:
#	valgrind --suppressions=ldmd.supp --gen-suppressions=all \
#	    --leak-check=full --show-reachable=yes --trace-children=no \
#	    --child-silent-after-fork=no ./valgrind-wrapper.sh \
#	    $(DESTDIR)$(bindir)/ldmd -l-

debug:		ldmd
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb ./ldmd

.c.i:
	$(COMPILE) -E -o $@ $<
    
if HAVE_CUNIT

debug_uldb:		testUpstreamLdmDb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute gdb testUpstreamLdmDb

valgrind_uldb:	testUpstreamLdmDb
	$(TESTS_ENVIRONMENT) $(LIBTOOL) --mode=execute valgrind \
	    --leak-check=full --show-reachable=yes ./testUpstreamLdmDb
        
endif

check-empty-conf: ./ldmd
	../pqcreate/pqcreate -c -s 10M -q ldmd.pq
	trap "rm ldmd.pq" EXIT; \
	if $(TESTS_ENVIRONMENT) ./ldmd -l- -I 127.0.0.1 -P 38800 -q ldmd.pq \
		/dev/null 2>/dev/null; then \
            exit 1; \
        else \
            exit 0; \
        fi

check-exec-stderr: ./ldmd
	../pqcreate/pqcreate -c -s 10M -q ldmd.pq
	echo 'ALLOW ANY ^((localhost|loopback)|(127\.0\.0\.1\.?$$))' \
		>check-exec-stderr.conf
	echo 'EXEC "../ulogger/ulogger -p ERROR This is an error"' \
		>>check-exec-stderr.conf
	rm -f ldmd.log
	$(TESTS_ENVIRONMENT) ./ldmd -l ldmd.log -I 127.0.0.1 -P 38800 \
                    -q ldmd.pq check-exec-stderr.conf >ldmd.pid
	sleep 2
	kill `cat ldmd.pid`
	pqcat -l ldmd.log -s -q ldmd.pq
	rm ldmd.pid check-exec-stderr.conf ldmd.pq

if WANT_MULTICAST
CHECK_MULTICAST	= check-multicast
check-multicast:
	../pqcreate/pqcreate -vl- -c -s 100M -q up7.pq
	../pqcreate/pqcreate -vl- -c -s 100M -q down7.pq
	echo 'ALLOW ANY .*' > up7.conf
	echo 'MULTICAST ANY 234.128.117.0:38800 1 127.0.0.1' >> up7.conf
	echo 'RECEIVE ANY localhost:38800' > down7.conf
	rm -f up7.log
	$(TESTS_ENVIRONMENT) ./ldmd -l up7.log -I 127.0.0.1 \
		-P 38800 -q up7.pq up7.conf >up7.pid
	sleep 1
	ps -o pid,ppid,pgid,args -p `cat up7.pid`
	rm -f down7.log
	$(TESTS_ENVIRONMENT) ./ldmd -vl down7.log -q down7.pq down7.conf \
               >down7.pid
	sleep 1
	if ! ps -o pid,ppid,pgid,args -p `cat down7.pid`; then \
	    echo "Downstream LDM-7 isn't running. See \"down7.log\"" 1>&2; \
            kill `cat up7.pid`; \
            exit 1; \
	fi
	rm -f pq_test_insert.log
	../pqinsert/pq_test_insert -vl pq_test_insert.log -q up7.pq -f NEXRAD3 \
		$(srcdir)/nexrad3.txt
	if ! ps -o pid,ppid,pgid,args -p `cat up7.pid`; then \
	    echo "Upstream LDM-7 isn't running. See \"up7.log\"" 1>&2; \
            kill `cat down7.pid`; \
            exit 1; \
	fi
	if ! ps -o pid,ppid,pgid,args -p `cat down7.pid`; then \
	    echo "Downstream LDM-7 isn't running. See \"down7.log\"" 1>&2; \
            kill `cat up7.pid`; \
            exit 1; \
	fi
	sleep 1
	kill `cat down7.pid` `cat up7.pid`
	rm down7.pid up7.pid
	rm up7.pq down7.pq
endif

check-local: check-empty-conf #$(CHECK_MULTICAST)
