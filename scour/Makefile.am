# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

# EXTRA_DIST		= scour.conf.in scour.1.in
# BUILT_SOURCES		= scour.conf
# dist_bin_SCRIPTS	= scour
# nodist_man1_MANS	= scour.1
# CLEANFILES              = scour.1 scour.conf
# DISTCLEANFILES		= scour scour.conf

# scour.1:	$(srcdir)/scour.1.in
# 	../regutil/substPaths <$? >$@.tmp
# 	mv $@.tmp $@

# scour.conf:	scour.conf.in
# 	../regutil/substPaths <$? >$@.tmp
# 	mv $@.tmp $@

# install-data-local:    \
#     $(DESTDIR)$(sysconfdir) $(DESTDIR)$(sysconfdir)/scour.conf
# $(DESTDIR)$(sysconfdir)/scour.conf:
# 	$(INSTALL_DATA) scour.conf $@

# $(DESTDIR)$(sysconfdir):
# 	mkdir $@

#####################################################################

# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in
#######################################################################
bin_PROGRAMS	= Cscour
#AM_CPPFLAGS	= \
#	            -I$(top_builddir)/log    -I$(top_srcdir)/log
	            
Cscour_SOURCES	= Cscour.c  parser.c
#_scour_LDADD	= $(top_builddir)/lib/libldm.la 
#TAGS_FILES	= \
#	            ../log/*.c ../log/*.h 
