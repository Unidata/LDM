# $Id: Makefile,v 1.1.2.1 2009/08/14 14:34:12 steve Exp $
#
#	Makefile for including libdict package into LDM package
#
include ../macros.make

INCLUDES	=
LIBRARY		= ../libldm.a
LINT_LIBRARY	=
HEADERS		= 
LIB_CSRCS	= \
	dict.c \
	hb_tree.c
PACKING_LIST = \
	depends \
	Makefile \
	$(LIB_CSRCS) \
	LICENSE \
	dict.h \
	dict_private.h \
	hashtable.h \
	hb_tree.h \
	pr_tree.h \
	rb_tree.h \
	skiplist.h \
	sp_tree.h \
	tr_tree.h \
	wb_tree.h

all:	archived_files

include ../rules.make
include depends
