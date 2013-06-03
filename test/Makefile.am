# Copyright 2013 University Corporation for Atmospheric Research
# All rights reserved.
#
# This file is part of the LDM package.  See the file LICENSE
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
# Process this file with automake(1) to produce Makefile.in
EXTRA_DIST	= redo_results.tab results.tab
distId		= $(PACKAGE)-$(VERSION)
distName	= $(distId).tar.gz
distPath	= $(top_srcdir)/$(distName)
topTmpDir	= /tmp/$(distId)

if MAINTAINER

HOSTS		= \
    flip \
    gilda \
    grizzard \
    harpo \
    hydro2 \
    imogene \
    moe \
    spock \
    sol

$(distPath):
	cd .. && $(MAKE) dist

# The following rule is executed on the remote system.  Setting
# MAKEFLAGS to the empty string on the make(1) invocation line appears
# to be necessary due to a bug in the AIX 5.1 make(1).
hostcheck: $(distArchive)
	@echo '################################################################'
	@echo "Building on `uname -n` with"
	@echo "    PATH=$$PATH"
	@echo "    CONFIG_OPTS=\"$(CONFIG_OPTS)\""
	@echo "    MAKE=\"$(MAKE)\""
	@echo "    CC=\"$(CC)\""
	@echo
	-test -d $(topTmpDir) && rm -rf $(topTmpDir)
	mkdir $(topTmpDir)
	gunzip -c $(distArchive) | (cd $(topTmpDir) && pax -r)
	if cd $(topTmpDir)/$(distId); then \
	    if ./configure --prefix=$(topTmpDir) --disable-root-actions \
		    $(CONFIG_OPTS) CC='$(CC)' MAKE="$(MAKE)" \
		    LDMHOME=$(topTmpDir); then \
		MAKEFLAGS= $(MAKE) install uninstall; \
		status=$$?; \
	    else \
		status=$$?; \
		cat config.log; \
	    fi; \
	    cd /tmp; \
	    rm -rf $(topTmpDir); \
	    exit $$status; \
	fi

test:
	for util in false true; do \
	    if echo '\
		if '$$util'; then \
		    status=$$?; \
		else \
		    status=$$?; \
		fi; \
		exit $$status' | ssh -x buddy; \
	    then \
		echo true; \
	    else \
		echo false; \
	    fi; \
	done

# The following rule starts a single synchronous build process on a remote
# system.
singleCheck:
	scp remoteCheck $(distPath) $(host):/tmp
	ssh -x $(host) chmod +x /tmp/remoteCheck
	if ssh -x $(host) /tmp/remoteCheck $(distId) \
	    /tmp/$(distName) '"$(env)"' '"$(configOpts)"' '"$(make)"' \
		'"$(cc)"' $(tmp); \
	then \
	    echo "$(env),$(make),$(cc),$(configOpts),1" >>$(host).results; \
	else \
	    echo "$(env),$(make),$(cc),$(configOpts),0" >>$(host).results; \
	fi
	ssh -x $(host) rm /tmp/remoteCheck /tmp/$(distName)
	touch redo_results.tab

# The following rule executes multiple synchronous build processes on a
# remote system for a single make(1) and cc(1) pair.
multiCheck:
	-$(MAKE) host="$(host)" env="$(env)" make="$(make)" cc="$(cc)" \
	    tmp="$(tmp)" configOpts="$(configOpts)" singleCheck
	-$(MAKE) host="$(host)" env="$(env)" make="$(make)" cc="$(cc)" \
	    tmp="$(tmp)" configOpts="--disable-shared $(configOpts)" singleCheck
	-$(MAKE) host="$(host)" env="$(env)" make="$(make)" cc="$(cc)" \
	    tmp="$(tmp)" \
	    configOpts="--with-noaaport --with-gribinsert $(configOpts)" \
	    singleCheck
	-$(MAKE) host="$(host)" env="$(env)" make="$(make)" cc="$(cc)" \
	    tmp="$(tmp)" \
	    configOpts="--with-noaaport --with-gribinsert --disable-shared $(configOpts)" \
	    singleCheck

# The following rule executes multiple synchronous build processes on a
# remote system for multiple make(1) and cc(1) pairs.
.host.results:
	rm -f $*.results $*.log
	grep '^[^#]' $< | while IFS=',' read env configOpts make cc tmp; do \
	    if ! $(MAKE) host="$*" env="$$env" configOpts="$$configOpts" \
		make="$$make" cc="$$cc" tmp="$$tmp" multiCheck </dev/null \
		    >>$*.log 2>&1; \
	    then \
		echo 1>&2 "Couldn't build on $*.  See file \"$*.log\"."; \
		exit 1; \
	    fi; \
	done
	echo Done: $*

# The following rule starts concurrent asynchronous builds on multiple remote
# systems.
hostchecks:
	rm -f *.results results.tab
	for host in $(HOSTS); do \
	    $(MAKE) -s $$host.results & \
	    echo "Building on $$host"; \
	done; \
	wait

# "results.tab" doesn't depend on $(HOSTS) because that would cause it
# to be recreated every time a "make ... singleCheck" was executed to debug
# the build on a remote system.
#
# FIELDS: hostname,os,cpu,make,cc,config_opts,exit_status
results.tab:	redo_results.tab
	for host in $(HOSTS); do \
	    (ssh -x $$host 'if test `uname` = AIX; then uname -srv; \
		    else uname -sr; fi; uname -m'; \
	    cat $$host.results) | \
	    awk 'NR==1{os=$$0}NR==2{cpu=$$0} \
		NR>2{print "'$$host'," os "," cpu "," $$0}'; \
	done >$@

portability.csv:	results.tab
	@test -e $@ || touch $@
	@successes=`grep ',1$$' results.tab | wc -l`; \
	failures=`grep ',0$$' results.tab | wc -l`; \
	tail -1 $@ | awk -F , '{time=$$1; prevSuccesses=$$2; prevFailures=$$3} \
	    END { \
		if ('$$successes' != prevSuccesses || \
			'$$failures' != prevFailures) { \
		    print "CHANGE IN PORTABILITY"; \
		    printf("  Successes:    %3d  (was %3d)\n", '$$successes', \
			prevSuccesses); \
		    printf("  Failures:     %3d  (was %3d)\n", '$$failures', \
			prevFailures); \
		    total = '$$successes' + '$$failures'; \
		    prevTotal = prevSuccesses + prevFailures; \
		    printf("  Total:        %3d  (was %3d)\n", total, \
			prevTotal); \
		    if (total != 0) { \
			printf("  Success Rate: %3d%%", \
			    int(100*'$$successes'/total)); \
		    } \
		    if (prevTotal != 0) { \
			printf(" (was %3d%)", \
			    int(100*prevSuccesses/prevTotal)); \
		    } \
		    print ""; \
		} \
	    }'; \
	echo `date +%Y%m%d.%H%M`,$$successes,$$failures >>$@

remote-checks:	hostchecks results.tab portability.csv

.PHONY:	singleCheck multiCheck hostchecks hostcheck remote-checks

endif

## The following entries are necessary to stop make(1)s that don't
## understand the .PHONY target from trying to create the given files.
install-info-am:
install-html-am:
install-pdf-am:
