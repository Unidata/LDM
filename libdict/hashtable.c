/*
 * hashtable.c
 *
 * Implementation for chained hash table.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: hashtable.c,v 1.1.2.1 2009/08/14 14:34:17 steve Exp $
 *
 * cf. [Gonnet 1984], [Knuth 1998]
 */

#include <stdlib.h>

#include "hashtable.h"
#include "dict_private.h"

/*
 * We store the untruncated hash value in the `hash' field. This speeds up
 * searching and allows us to resize without recomputing the original hash
 * value.
 *
 * If it weren't for iterators, there would be no reason have a `prev' field.
 */

typedef struct hash_node hash_node;
struct hash_node {
	void		*key;
	void		*dat;
	unsigned	 hash;
	hash_node	*next;
	hash_node	*prev;
};

struct hashtable {
	hash_node		**table;
	unsigned		  size;
	dict_cmp_func	  key_cmp;
	dict_hsh_func	  key_hash;
	dict_del_func	  key_del;
	dict_del_func	  dat_del;
	unsigned		  count;
};

struct hashtable_itor {
	hashtable	*table;
	hash_node	*node;
	unsigned	 slot;
};

hashtable *
hashtable_new(key_cmp, key_hash, key_del, dat_del, size)
	dict_cmp_func	key_cmp;
	dict_hsh_func	key_hash;
	dict_del_func	key_del;
	dict_del_func	dat_del;
	unsigned		size;
{
	hashtable *table;
	unsigned i;

	ASSERT(key_hash != NULL);
	ASSERT(size != 0);

	table = MALLOC(sizeof(*table));
	if (table == NULL)
		return NULL;
	table->table = MALLOC(size * sizeof(hash_node *));
	if (table->table == NULL) {
		FREE(table);
		return NULL;
	}
	for (i = 0; i < size; i++)
		table->table[i] = NULL;

	table->size = size;
	table->key_cmp = key_cmp ? key_cmp : _dict_key_cmp;
	table->key_hash = key_hash;
	table->key_del = key_del;
	table->dat_del = dat_del;
	table->count = 0;

	return table;
}

dict *
hashtable_dict_new(key_cmp, key_hash, key_del, dat_del, size)
	dict_cmp_func	key_cmp;
	dict_hsh_func	key_hash;
	dict_del_func	key_del;
	dict_del_func	dat_del;
	unsigned		size;
{
	dict *dct;
	hashtable *table;

	ASSERT(key_hash != NULL);
	ASSERT(size != 0);

	dct = MALLOC(sizeof(*dct));
	if (dct == NULL)
		return NULL;

	table = hashtable_new(key_cmp, key_hash, key_del, dat_del, size);
	if (table == NULL) {
		FREE(dct);
		return NULL;
	}

	dct->_object = table;
	dct->_inew = (inew_func)hashtable_dict_itor_new;
	dct->_destroy = (destroy_func)hashtable_destroy;
	dct->_insert = (insert_func)hashtable_insert;
	dct->_probe = (probe_func)hashtable_probe;
	dct->_search = (search_func)hashtable_search;
	dct->_csearch = (csearch_func)hashtable_csearch;
	dct->_remove = (remove_func)hashtable_remove;
	dct->_empty = (empty_func)hashtable_empty;
	dct->_walk = (walk_func)hashtable_walk;
	dct->_count = (count_func)hashtable_count;

	return dct;
}

void
hashtable_destroy(table, del)
	hashtable	*table;
	int			 del;
{
	ASSERT(table != NULL);

	hashtable_empty(table, del);
	FREE(table->table);
	FREE(table);
}

int
hashtable_insert(table, key, dat, overwrite)
	hashtable	*table;
	void		*key;
	void		*dat;
	int			 overwrite;
{
	unsigned hash;
	hash_node *node, *add;

	ASSERT(table != NULL);

	hash = table->key_hash(key);

	for (node = table->table[hash % table->size]; node; node = node->next)
		if (hash == node->hash && table->key_cmp(key, node->key) == 0) {
			if (overwrite == 0)
				return 1;
			if (table->key_del)
				table->key_del(node->key);
			if (table->dat_del)
				table->dat_del(node->dat);
			node->key = key;
			node->dat = dat;
			return 0;
		}

	add = MALLOC(sizeof(*add));
	if (add == NULL)
		return -1;
	add->key = key;
	add->dat = dat;
	add->hash = hash;
	add->prev = NULL;

	hash %= table->size;
	add->next = table->table[hash];
	if (table->table[hash])
		table->table[hash]->prev = add;
	table->table[hash] = add;
	table->count++;

	return 0;
}

int
hashtable_probe(table, key, dat)
	hashtable	 *table;
	void		 *key;
	void		**dat;
{
	unsigned hash, mhash;
	hash_node *node, *prev, *add;
	void *tmp;

	ASSERT(table != NULL);
	ASSERT(dat != NULL);

	hash = table->key_hash(key);
	mhash = hash % table->size;

	prev = NULL;
	node = table->table[mhash];
	for (; node; prev = node, node = node->next)
		if (hash == node->hash && table->key_cmp(key, node->key) == 0)
			break;
	if (node) {
		if (prev) { /* Tranpose. */
			SWAP(prev->key, node->key, tmp);
			SWAP(prev->dat, node->dat, tmp);
			SWAP(prev->hash, node->hash, hash);
			node = prev;
		}
		*dat = node->dat;
		return 0;
	}

	add = MALLOC(sizeof(*add));
	if (add == NULL)
		return -1;
	add->key = key;
	add->dat = *dat;
	add->hash = hash;
	add->prev = NULL;

	add->next = table->table[mhash];
	if (table->table[mhash])
		table->table[mhash]->prev = add;
	table->table[mhash] = add;
	table->count++;
	return 1;
}

void *
hashtable_search(table, key)
	hashtable	*table;
	const void	*key;
{
	unsigned hash;
	hash_node *node, *prev;
	void *tmp;

	ASSERT(table != NULL);

	hash = table->key_hash(key);
	prev = NULL;
	node = table->table[hash % table->size];
	for (; node; prev = node, node = node->next)
		if (hash == node->hash && table->key_cmp(key, node->key) == 0)
			break;
	if (node) {
		if (prev) {
			/*
			 * Tranpose. This typically offers better performance than move-to-
			 * front, but requires a fairly large number of accesses to
			 * take a randomly ordered chain and re-arrange it to nearly
			 * optimal. According to [Gonnet 1984] it may take Big-Omega(n^2)
			 * to come within 1+epsilon of the final state.
			 */
			SWAP(prev->key, node->key, tmp);
			SWAP(prev->dat, node->dat, tmp);
			SWAP(prev->hash, node->hash, hash);
			node = prev;
		}
		/* Node was already at front of list. */
		return node->dat;
	}
	return NULL;
}

const void *
hashtable_csearch(table, key)
	const hashtable	*table;
	const void		*key;
{
	ASSERT(table != NULL);

	/*
	 * Cast OK, we want to be able to tranpose, which doesnt modify the
	 * contents of table, only the ordering of items on the chain.
	 */
	return hashtable_search((hashtable *)table, key);
}

int
hashtable_remove(table, key, del)
	hashtable	*table;
	const void	*key;
	int			 del;
{
	hash_node *node, *prev;
	unsigned hash, mhash;

	ASSERT(table != NULL);

	hash = table->key_hash(key);
	mhash = hash % table->size;

	prev = NULL;
	node = table->table[mhash];
	for (; node; prev = node, node = node->next)
		if (hash == node->hash && table->key_cmp(key, node->key) == 0)
			break;
	if (node == NULL)
		return -1;

	if (prev)
		prev->next = node->next;
	else
		table->table[mhash] = node->next;

	if (node->next)
		node->next->prev = prev;

	if (del) {
		if (table->key_del)
			table->key_del(node->key);
		if (table->dat_del)
			table->dat_del(node->dat);
	}
	FREE(node);
	table->count--;
	return 0;
}

void
hashtable_empty(table, del)
	hashtable	*table;
	int			 del;
{
	hash_node *node, *next;
	unsigned slot;

	ASSERT(table != NULL);

	for (slot = 0; slot < table->size; slot++) {
		for (node = table->table[slot]; node; node = next) {
			next = node->next;
			if (del) {
				if (table->key_del)
					table->key_del(node->key);
				if (table->dat_del)
					table->dat_del(node->dat);
			}
			FREE(node);
		}
		table->table[slot] = NULL;
	}
}

void
hashtable_walk(table, visit)
	hashtable		*table;
	dict_vis_func	 visit;
{
	hash_node *node;
	unsigned i;

	ASSERT(table != NULL);
	ASSERT(visit != NULL);

	for (i = 0; i < table->size; i++)
		for (node = table->table[i]; node; node = node->next)
			if (visit(node->key, node->dat) == 0)
				goto out;
out:
	; /* MS compiler needs this. Thanks to John Day for the tip. */
}

unsigned
hashtable_count(table)
	const hashtable	*table;
{
	ASSERT(table != NULL);

	return table->count;
}

unsigned
hashtable_size(table)
	const hashtable	*table;
{
	ASSERT(table != NULL);

	return table->size;
}

unsigned
hashtable_slots_used(table)
	const hashtable	*table;
{
	unsigned i, count = 0;

	ASSERT(table != NULL);

	for (i = 0; i < table->size; i++)
		if (table->table[i])
			count++;
	return count;
}

int
hashtable_resize(table, size)
	hashtable	*table;
	unsigned	 size;
{
	struct hash_node **ntable;
	hash_node *node, *next;
	unsigned i, hash;

	ASSERT(table != NULL);
	ASSERT(size != 0);

	if (table->size == size)
		return 0;

	ntable = MALLOC(size * sizeof(hash_node *));
	if (ntable == NULL)
		return -1;

	for (i = 0; i < size; i++)
		ntable[i] = NULL;

	/*
	 * This way of resizing completely reverses(!) the effects of the trans-
	 * positions that we have been doing in probes and lookups. Hopefully
	 * resizing the hashtable is something that is done rarely or not at all,
	 * so this won't make too much difference.
	 */
	for (i = 0; i < table->size; i++) {
		for (node = table->table[i]; node; node = next) {
			next = node->next;
			hash = node->hash % size;
			node->next = ntable[hash];
			node->prev = NULL;
			if (ntable[hash])
				ntable[hash]->prev = node;
			ntable[hash] = node;
		}
	}

	FREE(table->table);
	table->table = ntable;
	table->size = size;

	return 0;
}

#define RETVALID(itor)	return itor->node != NULL

hashtable_itor *
hashtable_itor_new(table)
	hashtable *table;
{
	hashtable_itor *itor;

	ASSERT(table != NULL);

	itor = MALLOC(sizeof(*itor));
	if (itor == NULL)
		return NULL;

	itor->table = table;
	itor->node = NULL;
	itor->slot = 0;

	hashtable_itor_first(itor);
	return itor;
}

dict_itor *
hashtable_dict_itor_new(table)
	hashtable *table;
{
	dict_itor *itor;

	ASSERT(table != NULL);

	itor = MALLOC(sizeof(*itor));
	if (itor == NULL)
		return NULL;
	if ((itor->_itor = hashtable_itor_new(table)) == NULL) {
		FREE(itor);
		return NULL;
	}

	itor->_destroy = (idestroy_func)hashtable_itor_destroy;
	itor->_valid = (valid_func)hashtable_itor_valid;
	itor->_invalid = (invalidate_func)hashtable_itor_invalidate;
	itor->_next = (next_func)hashtable_itor_next;
	itor->_prev = (prev_func)hashtable_itor_prev;
	itor->_nextn = (nextn_func)hashtable_itor_nextn;
	itor->_prevn = (prevn_func)hashtable_itor_prevn;
	itor->_first = (first_func)hashtable_itor_first;
	itor->_last = (last_func)hashtable_itor_last;
	itor->_search = (isearch_func)hashtable_itor_search;
	itor->_key = (key_func)hashtable_itor_key;
	itor->_data = (data_func)hashtable_itor_data;
	itor->_cdata = (cdata_func)hashtable_itor_cdata;

	return itor;
}

void
hashtable_itor_destroy(itor)
	hashtable_itor *itor;
{
	ASSERT(itor != NULL);

	FREE(itor);
}

int
hashtable_itor_valid(itor)
	const hashtable_itor *itor;
{
	ASSERT(itor != NULL);

	RETVALID(itor);
}

void
hashtable_itor_invalidate(itor)
	hashtable_itor *itor;
{
	ASSERT(itor != NULL);

	itor->node = NULL;
	itor->slot = 0;
}

int
hashtable_itor_next(itor)
	hashtable_itor *itor;
{
	unsigned slot;
	hash_node *node;

	ASSERT(itor != NULL);

	if ((node = itor->node) == NULL)
		return hashtable_itor_first(itor);

	slot = itor->slot;
	node = node->next;
	if (node) {
		itor->node = node;
		return 1;
	}

	while (++slot < itor->table->size)
		if ((node = itor->table->table[slot]) != NULL)
			break;
	itor->node = node;
	itor->slot = node ? slot : 0;
	RETVALID(itor);
}

int
hashtable_itor_prev(itor)
	hashtable_itor *itor;
{
	unsigned slot;
	hash_node *node;

	ASSERT(itor != NULL);

	if ((node = itor->node) == NULL)
		return hashtable_itor_last(itor);

	slot = itor->slot;
	node = node->prev;
	if (node) {
		itor->node = node;
		return 1;
	}

	while (slot > 0)
		if ((node = itor->table->table[--slot]) != NULL) {
			for (; node->next; node = node->next)
				/* void */;
			break;
		}
	itor->node = node;
	itor->slot = slot;

	RETVALID(itor);
}

int
hashtable_itor_nextn(itor, count)
	hashtable_itor *itor;
	unsigned count;
{
	ASSERT(itor != NULL);

	if (!count)
		RETVALID(itor);

	while (count) {
		if (!hashtable_itor_next(itor))
			break;
		count--;
	}
	return count == 0;
}

int
hashtable_itor_prevn(itor, count)
	hashtable_itor *itor;
	unsigned count;
{
	ASSERT(itor != NULL);

	if (!count)
		RETVALID(itor);

	while (count) {
		if (!hashtable_itor_prev(itor))
			break;
		count--;
	}
	return count == 0;
}

int
hashtable_itor_first(itor)
	hashtable_itor *itor;
{
	unsigned slot;

	ASSERT(itor != NULL);

	for (slot = 0; slot < itor->table->size; slot++)
		if (itor->table->table[slot])
			break;
	if (slot == itor->table->size) {
		itor->node = NULL;
		slot = 0;
	} else {
		itor->node = itor->table->table[slot];
		itor->slot = (int)slot;
	}
	RETVALID(itor);
}

int
hashtable_itor_last(itor)
	hashtable_itor *itor;
{
	unsigned slot;

	ASSERT(itor != NULL);

	for (slot = itor->table->size; slot;)
		if (itor->table->table[--slot])
			break;
	if ((int)slot < 0) {
		itor->node = NULL;
		itor->slot = 0;
	} else {
		hash_node *node;

		for (node = itor->table->table[slot]; node->next; node = node->next)
			/* void */;
		itor->node = node;
		itor->slot = slot;
	}
	RETVALID(itor);
}

int
hashtable_itor_search(itor, key)
	hashtable_itor	*itor;
	const void		*key;
{
	hash_node *node;
	unsigned hash;

	hash = itor->table->key_hash(key);
	node = itor->table->table[hash % itor->table->size];
	for (; node; node = node->next)
		if (hash == node->hash && itor->table->key_cmp(key, node->key) == 0)
			break;
	itor->node = node;
	itor->slot = node ? hash % itor->table->size : 0;
	return node ? 1 : 0;
}

const void *
hashtable_itor_key(itor)
	const hashtable_itor *itor;
{
	ASSERT(itor != NULL);

	return itor->node ? itor->node->key : NULL;
}

void *
hashtable_itor_data(itor)
	hashtable_itor *itor;
{
	ASSERT(itor != NULL);

	return itor->node ? itor->node->dat : NULL;
}

const void *
hashtable_itor_cdata(itor)
	const hashtable_itor *itor;
{
	ASSERT(itor != NULL);

	return itor->node ? itor->node->dat : NULL;
}
