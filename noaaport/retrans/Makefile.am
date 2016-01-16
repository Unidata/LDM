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
			  -I$(top_srcdir)/log

bin_PROGRAMS            = acq_ldm_getshm acq_ldm_ctl acq_ldm_freeshm
ACQ_SHM_SOURCES         = shmem_lib.c \
			  acq_shm_lib.h
acq_ldm_getshm_SOURCES  = acq_ldm_getshm.c $(ACQ_SHM_SOURCES)
acq_ldm_freeshm_SOURCES = acq_ldm_freeshm.c $(ACQ_SHM_SOURCES)
acq_ldm_ctl_SOURCES     = acq_ldm_ctl.c $(ACQ_SHM_SOURCES)

install_setuids:
	chown $(DESTDIR)$(bindir)/acq_ldm_getshm
	chmod 4755 $(DESTDIR)$(bindir)/acq_ldm_getshm
	chown $(DESTDIR)$(bindir)/acq_ldm_ctl
	chmod 4755 $(DESTDIR)$(bindir)/acq_ldm_ctl
	chown $(DESTDIR)$(bindir)/acq_ldm_freeshm
	chmod 4755 $(DESTDIR)$(bindir)/acq_ldm_freeshm

TAGS_FILES		= *.c *.h \
                          $(top_srcdir)/ulog/*.c $(top_srcdir)/ulog/*.h \
                          $(top_builddir)/ulog/*.c $(top_builddir)/ulog/*.h
