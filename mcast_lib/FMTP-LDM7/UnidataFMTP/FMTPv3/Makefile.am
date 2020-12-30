# Copyright 2020 University Corporation for Atmospheric Research. All rights
# reserved. See the file COPYRIGHT in the top-level source-directory of this
# package for copying and redistribution conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST		= fmtpBase.cpp fmtpBase.h
SUBDIRS			= receiver sender SilenceSuppressor RateShaper
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= fmtpBase.cpp fmtpBase.h \
			  TcpBase.cpp TcpBase.h \
                          SslHelp.cpp SslHelp.h \
                          SessKeyCrypt.cpp SessKeyCrypt.h \
                          HmacImpl.cpp HmacImpl.h 
lib_la_CPPFLAGS		= -DLDM_LOGGING \
                          -I$(srcdir)/.. \
                          -I$(top_srcdir)/../../../log
lib_la_LIBADD		= receiver/lib.la sender/lib.la \
                          SilenceSuppressor/lib.la RateShaper/lib.la \
                          -lssl -lcrypto
CLEANFILES		= pdu

noinst_PROGRAMS		= IperfFmtpPdu
IperfFmtpPdu_SOURCES	= IperfFmtpPdu.cpp
