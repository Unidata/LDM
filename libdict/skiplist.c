/*
 * skiplist.c
 *
 * Implementation of height balanced tree.
 * Copyright (C) 2001 Farooq Mela. All rights reserved.
 *
 * $Id: skiplist.c,v 1.1.2.1 2009/08/14 14:34:21 steve Exp $
 *
 * cf. [Pugh 1990]
 */

#include <stdlib.h>

#include "skiplist.h"
#include "dict_private.h"

struct skipnode {
	void		*key;
	void		*dat;
	skipnode	*prev;		/* Previous node.				*/
	unsigned	 nlinks;	/* Number of forward links.		*/
	skipnode	*links[1];	/* Array of forward links.		*/
};

struct skiplist {
	unsigned	maxlinks;
	unsigned	lg_n;
	unsigned	count;
	skipnode	head;
};

static void skiprand(skiplist *list);

skiplist *
skiplist_new(key_cmp, key_del, dat_del, maxlinks)
	dict_cmp_func	key_cmp;
	dict_del_func	key_del;
	dict_del_func	dat_del;
	unsigned		maxlinks;
{
	skiplist *list;

	ASSERT(maxlinks != 0);

	if ((list = MALLOC(sizeof(*list))) == NULL)
		return NULL;

	list->maxlinks = maxlinks;
	list->lg_n = 0;
	list->head.key = NULL;
	list->head.dat = NULL;
	list->head.nlinks = 0;

	return list;
}

dict *
skiplist_dict_new(key_cmp, key_del, dat_del, maxlinks)
	dict_cmp_func	key_cmp;
	dict_del_func	key_del;
	dict_del_func	dat_del;
	unsigned		maxlinks;
{
	dict *dct;
	skiplist *list;

	ASSERT(maxlinks != 0);

	if ((dct = MALLOC(sizeof(*dct))) == NULL)
		return NULL;

	if ((list = skiplist_new(key_cmp, key_del, dat_del, maxlinks)) == NULL) {
		FREE(dct);
		return NULL;
	}

	dct->_object = list;
	dct->_inew = (inew_func)skiplist_dict_itor_new;
	dct->_destroy = (destroy_func)skiplist_destroy;
	dct->_insert = (insert_func)skiplist_destroy;
	dct->_probe = (probe_func)skiplist_destroy;
	dct->_search = (search_func)skiplist_destroy;
	dct->_csearch = (csearch_func)skiplist_destroy;
	dct->_remove = (remove_func)skiplist_destroy;
	dct->_empty = (empty_func)skiplist_destroy;
	dct->_walk = (walk_func)skiplist_destroy;
	dct->_count = (count_func)skiplist_destroy;

	return dct;
}

void
skiplist_destroy(list, del)
	skiplist	*list;
	int			 del;
{
	ASSERT(list != NULL);

	if (list->count)
		skiplist_empty(list, del);

	FREE(list);
}

void
skiplist_empty(list, del)
	skiplist	*list;
	int			 del;
{
	skipnode *node, *next;

	for (node = list->head.links[0]; node; node = next) {
		next = node->links[0];
		FREE(node->links);
		FREE(node);
}
