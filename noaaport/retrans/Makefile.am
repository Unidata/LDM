# Copyright 2014 University Corporation for Atmospheric Research
#
# This file is part of the Unidata NOAAPORT package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
## Process this file with automake(1) to produce file Makefile.in

LDMHOME			= @LDMHOME@

noinst_LTLIBRARIES	= lib.la
lib_la_SOURCES		= retrans.c \
                          retrans.h \
                          acq_shm_lib.h \
                          shmem_lib.c

lib_la_CPPFLAGS		= -DLDMHOME='"$(LDMHOME)"' \
			  -I$(top_srcdir) \
			  -I$(top_srcdir)/ulog

bin_PROGRAMS            = acq_ldm_getshm acq_ldm_ctl acq_ldm_freeshm

acq_ldm_getshm_SOURCES  = acq_ldm_getshm.c shmem_lib.c
acq_ldm_freeshm_SOURCES = acq_ldm_freeshm.c shmem_lib.c
acq_ldm_ctl_SOURCES     = acq_ldm_ctl.c shmem_lib.c

TAGS_FILES		= *.c *.h \
                          $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h
