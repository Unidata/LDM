# Copyright 2014 University Corporation for Atmospheric Research. All rights
# reserved. See the file COPYRIGHT in the top-level source-directory of this
# package for copying and redistribution conditions.
#
# Process this file with automake(1) to produce file Makefile.in

SUBDIRS 		= FMTPv3 test
ACLOCAL_AMFLAGS         = -I m4
EXTRA_DIST		= m4 CHANGE_LOG COPYRIGHT 
noinst_LTLIBRARIES	= libfmtp.la
libfmtp_la_SOURCES	= dummy.cpp
libfmtp_la_LIBADD	= FMTPv3/lib.la
dist_doc_data		= CHANGE_LOG COPYRIGHT README

version.m4:             CHANGE_LOG
	versionId=`awk '{print $1;exit}' $?`; \
	test "$versionId"; \
	echo "m4_define([VERSION_ID], [$$versionId])" >$@
