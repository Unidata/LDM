# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata LDM package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
# Process this file with automake(1) to produce file Makefile.in

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= ProdIndexDelayQueue.cpp ProdIndexDelayQueue.h \
                          RetxThreads.cpp RetxThreads.h \
			  senderMetadata.cpp senderMetadata.h \
			  SendProxy.h \
			  TcpSend.cpp TcpSend.h \
			  UdpSend.cpp UdpSend.h \
			  fmtpSendv3.cpp fmtpSendv3.h
lib_la_CPPFLAGS		= -I$(srcdir)/..
