# Copyright 2011 University Corporation for Atmospheric Research
#
# This file is part of the Unidata NOAAPORT package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
## Process this file with automake(1) to produce file Makefile.in

LDMHOME			= @LDMHOME@

SUBDIRS			= tables

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= \
	cflclos.c \
	cflinqr.c \
	cfliret.c \
	cflpath.c \
	cflropn.c \
	cfltbnr.c \
	cfltbop.c \
	cfltinq.c \
	cfltrln.c \
	cssenvr.c \
	cstctoi.c \
	cstinch.c \
	cstitoc.c \
	cstlcuc.c \
	cstldsp.c \
	cstlstr.c \
	cstncat.c \
	cstncpy.c \
	cstnocc.c \
	cstrmbl.c \
	cstrmst.c \
	cstrpst.c \
	cstrxbl.c \
	cstsplit.c \
	cstuclc.c \
	ctbcmn.h \
	ctbg2gnam.c \
	ctbg2gnum.c \
	ctbg2rdcntr.c \
	ctbg2rdlvl.c \
	ctbg2rdvar.c \
	ctbg2read.c \
	decode_g2gnum.c \
	error.h \
	erwmsg.c \
	gb22gem.c \
	gb2ctim.c \
	gb2def.h \
	gb2fcsm.c \
	gb2ftim.c \
	gb2gtcntr.c \
	gb2gtlcllvltbl.c \
	gb2gtlclvartbl.c \
	gb2gtwmolvltbl.c \
	gb2gtwmovartbl.c \
	gb2param.c \
	gb2prob.c \
	gb2proc.c \
	gb2sklvl.c \
	gb2skvar.c \
	gb2vcrd.c \
	gb2vlev.c \
	gbcmn.h \
	geminc.h \
	gemprm.h \
	lvcord.c \
	proto_cgemlib.h \
	proto_gemlib.h \
	proto.h \
	tgitoc.c \
	tiaddm.c \
	tidaym.c \
	tiitoj.c \
	timdif.c

lib_la_CPPFLAGS		= -DLDMHOME='"$(LDMHOME)"' \
			  -I$(top_srcdir) \
			  -I$(top_srcdir)/grib2 \
			  -I$(top_srcdir)/ulog \
			  -I$(top_builddir)/protocol -I$(top_srcdir)/protocol \
			  -I$(top_builddir)/protocol2 -I$(top_srcdir)/protocol2 \
			  -I$(top_builddir)/registry -I$(top_srcdir)/registry

TAGS_FILES		= \
	*.c *.h \
	../ulog/*.c ../ulog/*.h \
	../grib2/*.c ../grib2/*.h
