# Copyright 2009 University Corporation for Atmospheric Research
#
# This file is part of the LDM package.  See the file COPYRIGHT
# in the top-level source-directory of the package for copying and
# redistribution conditions.
#
## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= \
    auth_none.c \
    auth_unix.c \
    authunix_prot.c \
    bindresvport.c \
    clnt_generic.c \
    clnt_perror.c \
    clnt_raw.c \
    clnt_simple.c \
    clnt_tcp.c \
    clnt_udp.c \
    rpc_dtablesize.c \
    get_myaddress.c \
    getrpcport.c \
    pmap_clnt.c \
    pmap_getmaps.c \
    pmap_getport.c \
    pmap_prot.c \
    pmap_prot2.c \
    pmap_rmt.c \
    rpc_prot.c \
    rpc_callmsg.c \
    rpc_commondata.c \
    svc.c \
    svc_auth.c \
    svc_auth_unix.c \
    svc_raw.c \
    svc_run.c \
    svc_simple.c \
    svc_tcp.c \
    svc_udp.c \
    xdr.c \
    xdr_array.c \
    xdr_float.c \
    xdr_mem.c \
    xdr_rec.c \
    xdr_reference.c \
    xdr_stdio.c
lib_la_CPPFLAGS		= -I$(top_srcdir)/mylog
EXTRA_DIST		= \
    auth.h \
    auth_unix.h \
    clnt.h \
    pmap_clnt.h \
    pmap_prot.h \
    pmap_rmt.h \
    rpc.h \
    rpc_msg.h \
    svc.h \
    svc_auth.h \
    types.h \
    xdr.h
