# Copyright 2020 University Corporation for Atmospheric Research. All rights
# reserved. See the file COPYRIGHT in the top-level source-directory of this
# package for copying and redistribution conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST		= fmtpBase.cpp fmtpBase.h
SUBDIRS			= receiver sender SilenceSuppressor RateShaper
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= fmtpBase.cpp     fmtpBase.h \
			  TcpBase.cpp      TcpBase.h \
                          SslHelp.cpp      SslHelp.h \
                          SessKeyCrypt.cpp SessKeyCrypt.h \
                          HmacImpl.cpp     HmacImpl.h \
                          PubKeyCrypt.cpp  PubKeyCrypt.h
lib_la_CPPFLAGS		= -DLDM_LOGGING \
                          -I$(srcdir)/.. \
                          -I$(top_srcdir)/../../../log
lib_la_LIBADD		= receiver/lib.la sender/lib.la \
                          SilenceSuppressor/lib.la RateShaper/lib.la \
                          -lssl -lcrypto
CLEANFILES		= pdu

noinst_PROGRAMS		= IperfFmtpPdu
IperfFmtpPdu_SOURCES	= IperfFmtpPdu.cpp

check_PROGRAMS		= ecdsa_test2
#check_PROGRAMS		+= ecc_test

ecdsa_test2_SOURCES	= ecdsa_test2.cpp ecdsa.cpp
ecdsa_test2_LDADD	= -lgtest -lssl -lcrypto

#ecc_test_LDADD		= -lcunit -lssl -lcrypto

TESTS			= $(check_PROGRAMS)