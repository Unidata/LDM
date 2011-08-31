# Copyright 2011 University Corporation for Atmospheric Research
#
# This file is part of the Unidata NOAAPORT package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

#  If you want to enable support for PNG or JPEG2000 encoding/decoding,
#  you must specify -DUSE_PNG and/or -DUSE_JPEG2000 in the DEFS variable
#  for the C pre-processor
#  -DUSE_PNG requires libpng.a and libz.a
#  -DUSE_JPEG2000 requires libjasper.a
#
#
#DEFS=-DUSE_JPEG2000 -DUSE_PNG
#
#
#   This "C" source code conatins many uses of the C++ 
#   comment style "//".  Please make sure you include the
#   appropriate compiler flag to allow the use of "//" comment indicators.
#
#CFLAGS=-q64 -O3 -qarch=auto -qcpluscmt $(INC) $(DEFS)
#CC=c99
#ARFLAGS=-X64

#JAS_INC=-I$(NAWIPS)/jasper/$(NA_OS)/include
#PNG_INC=-I$(GEMPAK)/source/libpng

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= \
	cmplxpack.c \
	compack.c \
	comunpack.c \
	dec_jpeg2000.c \
	dec_png.c \
	drstemplates.c \
	drstemplates.h \
	enc_jpeg2000.c \
	enc_png.c \
	g2_addfield.c \
	g2_addgrid.c \
	g2_addlocal.c \
	g2_create.c \
	g2_free.c \
	g2_getfld.c \
	g2_gribend.c \
	g2_info.c \
	g2_miss.c \
	g2_pack_gp.c \
	g2_rdieee.c \
	g2_reduce.c \
	g2_unpack1.c \
	g2_unpack2.c \
	g2_unpack3.c \
	g2_unpack4.c \
	g2_unpack5.c \
	g2_unpack6.c \
	g2_unpack7.c \
	gbits.c \
	getdim.c \
	getpoly.c \
	grib2.h \
	gridtemplates.c \
	gridtemplates.h \
	int_power.c \
	jpcpack.c \
	jpcunpack.c \
	misspack.c \
	mkieee.c \
	pdstemplates.c \
	pdstemplates.h \
	pngpack.c \
	pngunpack.c \
	seekgb.c \
	simpack.c \
	simunpack.c \
	specpack.c \
	specunpack.c
