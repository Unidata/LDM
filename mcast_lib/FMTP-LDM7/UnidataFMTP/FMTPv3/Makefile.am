# Copyright 2020 University Corporation for Atmospheric Research. All rights
# reserved. See the file COPYRIGHT in the top-level source-directory of this
# package for copying and redistribution conditions.
#
# Process this file with automake(1) to produce file Makefile.in

EXTRA_DIST		= fmtpBase.cpp fmtpBase.h
SUBDIRS			= receiver sender SilenceSuppressor RateShaper
noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= FmtpBase.cpp     FmtpBase.h \
			  TcpBase.cpp      TcpBase.h \
			  SslHelp.cpp      SslHelp.h \
			  PubKeyCrypt.cpp  PubKeyCrypt.h \
			  Ed25519.cpp      Ed25519.h \
			  mac.cpp          mac.h
lib_la_CPPFLAGS		= -DLDM_LOGGING \
                          -I$(srcdir)/.. \
                          -I$(top_srcdir)/../../../log
lib_la_LIBADD		= receiver/lib.la sender/lib.la \
                          SilenceSuppressor/lib.la RateShaper/lib.la \
                          -lssl -lcrypto
CLEANFILES		= pdu

check_PROGRAMS		= Ed25519_test
#check_PROGRAMS		+= ecc_test

Ed25519_test_SOURCES	= Ed25519_test.cpp Ed25519.cpp SslHelp.cpp
Ed25519_test_LDADD	= -lgtest -lssl -lcrypto

mac_test_SOURCES	= mac_test.cpp mac.cpp Ed25519.cpp SslHelp.cpp
mac_test_LDADD		= -lgtest -lssl -lcrypto
check_PROGRAMS		+= mac_test

#ecc_test_LDADD		= -lcunit -lssl -lcrypto

TESTS			= $(check_PROGRAMS)