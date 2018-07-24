# Copyright 2018 University Corporation for Atmospheric Research
#
# This file is part of the Unidata NOAAPORT package.  See the file COPYRIGHT in
# the top-level source-directory of the package for copying and redistribution
# conditions.
#
## Process this file with automake(1) to produce file Makefile.in

G2VARSNCEP_s	= g2varsncep0.tbl g2varsncep1.tbl
G2VARSNSSL_s	= g2varsnssl1.tbl
G2VARSWMO_s	= g2varswmo0.tbl g2varswmo1.tbl g2varswmo2.tbl g2varswmo3.tbl \
		  g2varswmo6.tbl
G2VCRDNCEP_s	= g2vcrdncep0.tbl g2vcrdncep1.tbl
G2VCRDWMO_s	= g2vcrdwmo0.tbl g2vcrdwmo1.tbl g2vcrdwmo2.tbl g2vcrdwmo3.tbl \
		  g2vcrdwmo6.tbl g2vcrdwmo255.tbl 
LINKS		= $(G2VARSNCEP_s) \
		  $(G2VARSNSSL_s) \
		  $(G2VARSWMO_s) \
		  $(G2VCRDNCEP_s) \
		  $(G2VCRDWMO_s)

dist_sysconf_DATA	= \
	g2varsncep.tbl \
	g2varsncep-master.tbl \
	g2varsnssl.tbl \
	g2varswmo.tbl \
	g2vcrdncep.tbl \
	g2vcrdwmo.tbl \
	grdnav.tbl \
	wmocenter.tbl

install-data-hook:
	if cd $(DESTDIR)$(sysconfdir); then \
	    for link in $(LINKS); do \
		rm -f $$link; \
		file=`echo $$link | sed 's/[0-9]*\.tbl/.tbl/'`; \
		ln -s $$file $$link; \
	    done; \
	fi
