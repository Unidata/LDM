# Copyright 2010 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

EXTRA_DIST		= refresh_logging.in
BUILT_SOURCES		= refresh_logging
bin_PROGRAMS		= hupsyslog
bin_SCRIPTS 		= refresh_logging
hupsyslog_LDADD		= $(top_builddir)/lib/libldm.la
CLEANFILES		= $(BUILT_SOURCES)

refresh_logging:	refresh_logging.in
if WANT_ULOG
	echo hupsyslog >$@
else
	sed 's;$$LDMHOME;@LDMHOME@;' $(srcdir)/refresh_logging.in >$@
endif
	chmod +x $@