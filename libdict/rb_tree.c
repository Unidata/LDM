/*
 * rb_tree.c
 *
 * Implementation of red-black binary search tree.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: rb_tree.c,v 1.1.2.1 2009/08/14 14:34:20 steve Exp $
 *
 * cf. [Cormen, Leiserson, and Rivest 1990], [Guibas and Sedgewick, 1978]
 */

#include <stdlib.h>

#include "rb_tree.h"
#include "dict_private.h"

typedef struct rb_node rb_node;
struct rb_node {
	void		*key;
	void		*dat;
	rb_node		*parent;
	rb_node		*llink;
	rb_node		*rlink;
	unsigned	 color:1;
};

#define RB_RED			0
#define RB_BLK			1

struct rb_tree {
	rb_node			*root;
	unsigned		 count;
	dict_cmp_func	 key_cmp;
	dict_del_func	 key_del;
	dict_del_func	 dat_del;
};

struct rb_itor {
	rb_tree *tree;
	rb_node *node;
};

static rb_node _null = { NULL, NULL, NULL, NULL, NULL, RB_BLK };
#define RB_NULL	&_null

static void rot_left __P((rb_tree *tree, rb_node *node));
static void rot_right __P((rb_tree *tree, rb_node *node));
static void insert_fixup __P((rb_tree *tree, rb_node *node));
static void delete_fixup __P((rb_tree *tree, rb_node *node));
static unsigned node_height __P((const rb_node *node));
static unsigned node_mheight __P((const rb_node *node));
static unsigned node_pathlen __P((const rb_node *node, unsigned level));
static rb_node *node_new __P((void *key, void *dat));
static rb_node *node_next __P((rb_node *node));
static rb_node *node_prev __P((rb_node *node));
static rb_node *node_max __P((rb_node *node));
static rb_node *node_min __P((rb_node *node));

rb_tree *
rb_tree_new(key_cmp, key_del, dat_del)
	dict_cmp_func	key_cmp;
	dict_del_func	key_del;
	dict_del_func	dat_del;
{
	rb_tree *tree;

	tree = MALLOC(sizeof(*tree));
	if (tree) {
		tree->root = RB_NULL;
		tree->count = 0;
		tree->key_cmp = key_cmp ? key_cmp : _dict_key_cmp;
		tree->key_del = key_del;
		tree->dat_del = dat_del;
	}

	return tree;
}

dict *
rb_dict_new(key_cmp, key_del, dat_del)
	dict_cmp_func	key_cmp;
	dict_del_func	key_del;
	dict_del_func	dat_del;
{
	dict *dct;
	rb_tree *tree;

	dct = MALLOC(sizeof(*dct));
	if (dct == NULL)
		return NULL;

	if ((tree = rb_tree_new(key_cmp, key_del, dat_del)) == NULL) {
		FREE(dct);
		return NULL;
	}

	dct->_object = tree;
	dct->_inew = (inew_func)rb_dict_itor_new;
	dct->_destroy = (destroy_func)rb_tree_destroy;
	dct->_insert = (insert_func)rb_tree_insert;
	dct->_probe = (probe_func)rb_tree_probe;
	dct->_search = (search_func)rb_tree_search;
	dct->_csearch = (csearch_func)rb_tree_csearch;
	dct->_remove = (remove_func)rb_tree_remove;
	dct->_empty = (empty_func)rb_tree_empty;
	dct->_walk = (walk_func)rb_tree_walk;
	dct->_count = (count_func)rb_tree_count;

	return dct;
}

void
rb_tree_destroy(tree, del)
	rb_tree	*tree;
	int del;
{
	ASSERT(tree != NULL);

	if (tree->root != RB_NULL)
		rb_tree_empty(tree, del);

	FREE(tree);
}

void *
rb_tree_search(tree, key)
	rb_tree		*tree;
	const void	*key;
{
	int rv;
	rb_node *node;

	ASSERT(tree != NULL);

	node = tree->root;
	while (node != RB_NULL) {
		rv = tree->key_cmp(key, node->key);
		if (rv == 0)
			return node->dat;
		node = rv < 0 ? node->llink : node->rlink;
	}

	return NULL;
}

const void *
rb_tree_csearch(tree, key)
	const rb_tree	*tree;
	const void		*key;
{
	ASSERT(tree != NULL);

	return rb_tree_search((rb_tree *)tree, key);
}

int
rb_tree_insert(tree, key, dat, overwrite)
	rb_tree	*tree;
	void	*key;
	void	*dat;
	int		 overwrite;
{
	int rv = 0;
	rb_node *node, *parent = RB_NULL;

	ASSERT(tree != NULL);

	node = tree->root;

	while (node != RB_NULL) {
		rv = tree->key_cmp(key, node->key);
		if (rv == 0) {
			if (overwrite == 0)
				return 1;
			if (tree->key_del)
				tree->key_del(node->key);
			if (tree->dat_del)
				tree->dat_del(node->dat);
			node->key = key;
			node->dat = dat;
			return 0;
		}

		parent = node;
		node = rv < 0 ? node->llink : node->rlink;
	}

	node = node_new(key, dat);
	if (node == NULL)
		return -1;

	if (parent != RB_NULL) {
		node->parent = parent;
		if (rv < 0)
			parent->llink = node;
		else
			parent->rlink = node;
		insert_fixup(tree, node);
	} else {
		tree->root = node;
		node->color = RB_BLK;
	}
	tree->count++;
	return 0;
}

int
rb_tree_probe(tree, key, dat)
	rb_tree		 *tree;
	void		 *key;
	void		**dat;
{
	int rv = 0;
	rb_node *node, *parent = RB_NULL;

	ASSERT(tree != NULL);

	node = tree->root;

	while (node != RB_NULL) {
		rv = tree->key_cmp(key, node->key);
		if (rv == 0) {
			*dat = node->dat;
			return 0;
		}

		parent = node;
		node = rv < 0 ? node->llink : node->rlink;
	}

	node = node_new(key, *dat);
	if (node == NULL)
		return -1;

	if (parent != RB_NULL) {
		node->parent = parent;
		if (rv < 0)
			parent->llink = node;
		else
			parent->rlink = node;
		insert_fixup(tree, node);
	} else {
		tree->root = node;
		node->color = RB_BLK;
	}
	tree->count++;
	return 1;
}

void
insert_fixup(tree, node)
	rb_tree	*tree;
	rb_node	*node;
{
	rb_node *temp;

	ASSERT(tree != NULL);
	ASSERT(node != NULL);

	while (node != tree->root && node->parent->color == RB_RED) {
		if (node->parent == node->parent->parent->llink) {
			temp = node->parent->parent->rlink;
			if (temp->color == RB_RED) {
				temp->color = RB_BLK;
				node = node->parent;
				node->color = RB_BLK;
				node = node->parent;
				node->color = RB_RED;
			} else {
				if (node == node->parent->rlink) {
					node = node->parent;
					rot_left(tree, node);
				}
				temp = node->parent;
				temp->color = RB_BLK;
				temp = temp->parent;
				temp->color = RB_RED;
				rot_right(tree, temp);
			}
		} else {
			temp = node->parent->parent->llink;
			if (temp->color == RB_RED) {
				temp->color = RB_BLK;
				node = node->parent;
				node->color = RB_BLK;
				node = node->parent;
				node->color = RB_RED;
			} else {
				if (node == node->parent->llink) {
					node = node->parent;
					rot_right(tree, node);
				}
				temp = node->parent;
				temp->color = RB_BLK;
				temp = temp->parent;
				temp->color = RB_RED;
				rot_left(tree, temp);
			}
		}
	}

	tree->root->color = RB_BLK;
}

int
rb_tree_remove(tree, key, del)
	rb_tree		*tree;
	const void	*key;
	int			 del;
{
	int rv;
	rb_node *node, *temp, *out, *parent;
	void *tmp;

	ASSERT(tree != NULL);

	node = tree->root;
	while (node != RB_NULL) {
		rv = tree->key_cmp(key, node->key);
		if (rv == 0)
			break;
		node = rv < 0 ? node->llink : node->rlink;
	}
	if (node == RB_NULL)
		return -1;

	if (node->llink == RB_NULL || node->rlink == RB_NULL) {
		out = node;
	} else {
		for (out = node->rlink; out->llink != RB_NULL; out = out->llink)
			/* void */;
		SWAP(node->key, out->key, tmp);
		SWAP(node->dat, out->dat, tmp);
	}

	temp = out->llink != RB_NULL ? out->llink : out->rlink;
	parent = out->parent;
	temp->parent = parent;
	if (parent != RB_NULL) {
		if (parent->llink == out)
			parent->llink = temp;
		else
			parent->rlink = temp;
	} else {
		tree->root = temp;
	}

	if (out->color == RB_BLK)
		delete_fixup(tree, temp);
	if (del) {
		if (tree->key_del)
			tree->key_del(out->key);
		if (tree->dat_del)
			tree->dat_del(out->dat);
	}
	FREE(out);

	tree->count--;

	return 0;
}

static void
delete_fixup(tree, node)
	rb_tree *tree;
	rb_node *node;
{
	rb_node *temp;

	ASSERT(tree != NULL);
	ASSERT(node != NULL);

	while (node != tree->root && node->color == RB_BLK) {
		if (node->parent->llink == node) {
			temp = node->parent->rlink;
			if (temp->color == RB_RED) {
				temp->color = RB_BLK;
				node->parent->color = RB_RED;
				rot_left(tree, node->parent);
				temp = node->parent->rlink;
			}
			if (temp->llink->color == RB_BLK && temp->rlink->color == RB_BLK) {
				temp->color = RB_RED;
				node = node->parent;
			} else {
				if (temp->rlink->color == RB_BLK) {
					temp->llink->color = RB_BLK;
					temp->color = RB_RED;
					rot_right(tree, temp);
					temp = node->parent->rlink;
				}
				temp->color = node->parent->color;
				temp->rlink->color = RB_BLK;
				node->parent->color = RB_BLK;
				rot_left(tree, node->parent);
				node = tree->root;
			}
		} else {
			temp = node->parent->llink;
			if (temp->color == RB_RED) {
				temp->color = RB_BLK;
				node->parent->color = RB_RED;
				rot_right(tree, node->parent);
				temp = node->parent->llink;
			}
			if (temp->rlink->color == RB_BLK && temp->llink->color == RB_BLK) {
				temp->color = RB_RED;
				node = node->parent;
			} else {
				if (temp->llink->color == RB_BLK) {
					temp->rlink->color = RB_BLK;
					temp->color = RB_RED;
					rot_left(tree, temp);
					temp = node->parent->llink;
				}
				temp->color = node->parent->color;
				node->parent->color = RB_BLK;
				temp->llink->color = RB_BLK;
				rot_right(tree, node->parent);
				node = tree->root;
			}
		}
	}

	node->color = RB_BLK;
}

void
rb_tree_empty(tree, del)
	rb_tree		*tree;
	int			 del;
{
	rb_node *node, *parent;

	ASSERT(tree != NULL);

	node = tree->root;
	while (node != RB_NULL) {
		parent = node->parent;
		if (node->llink != RB_NULL) {
			node = node->llink;
			continue;
		}
		if (node->rlink != RB_NULL) {
			node = node->rlink;
			continue;
		}

		if (del) {
			if (tree->key_del)
				tree->key_del(node->key);
			if (tree->dat_del)
				tree->dat_del(node->dat);
		}
		FREE(node);

		if (parent != RB_NULL) {
			if (parent->llink == node)
				parent->llink = RB_NULL;
			else
				parent->rlink = RB_NULL;
		}
		node = parent;
	}

	tree->root = RB_NULL;
	tree->count = 0;
}

unsigned
rb_tree_count(tree)
	const rb_tree *tree;
{
	ASSERT(tree != NULL);

	return tree->count;
}

unsigned
rb_tree_height(tree)
	const rb_tree *tree;
{
	ASSERT(tree != NULL);

	return tree->root != RB_NULL ? node_height(tree->root) : 0;
}

unsigned
rb_tree_mheight(tree)
	const rb_tree *tree;
{
	ASSERT(tree != NULL);

	return tree->root != RB_NULL ? node_mheight(tree->root) : 0;
}

unsigned
rb_tree_pathlen(tree)
	const rb_tree *tree;
{
	ASSERT(tree != NULL);

	return tree->root != RB_NULL ? node_pathlen(tree->root, 1) : 0;
}

const void *
rb_tree_min(tree)
	const rb_tree *tree;
{
	const rb_node *node;

	ASSERT(tree != NULL);

	if (tree->root == RB_NULL)
		return NULL;

	for (node = tree->root; node->llink != RB_NULL; node = node->llink)
		/* void */;
	return node->key;
}

const void *
rb_tree_max(tree)
	const rb_tree *tree;
{
	const rb_node *node;

	ASSERT(tree != NULL);

	if (tree->root == RB_NULL)
		return NULL;

	for (node = tree->root; node->rlink != RB_NULL; node = node->rlink)
		/* void */;
	return node->key;
}

void
rb_tree_walk(tree, visit)
	rb_tree *tree;
	dict_vis_func visit;
{
	rb_node *node;

	ASSERT(tree != NULL);
	ASSERT(visit != NULL);

	if (tree->root == RB_NULL)
		return;

	for (node = node_min(tree->root); node != RB_NULL; node = node_next(node))
		if (visit(node->key, node->dat) == 0)
			break;
}

static unsigned
node_height(node)
	const rb_node *node;
{
	unsigned l, r;

	l = node->llink != RB_NULL ? node_height(node->llink) + 1 : 0;
	r = node->rlink != RB_NULL ? node_height(node->rlink) + 1 : 0;
	return MAX(l, r);
}

static unsigned
node_mheight(node)
	const rb_node *node;
{
	unsigned l, r;

	l = node->llink != RB_NULL ? node_mheight(node->llink) + 1 : 0;
	r = node->rlink != RB_NULL ? node_mheight(node->rlink) + 1 : 0;
	return MIN(l, r);
}

static unsigned
node_pathlen(node, level)
	const rb_node	*node;
	unsigned		 level;
{
	unsigned n = 0;

	ASSERT(node != RB_NULL);

	if (node->llink != RB_NULL)
		n += level + node_pathlen(node->llink, level + 1);
	if (node->rlink != RB_NULL)
		n += level + node_pathlen(node->rlink, level + 1);
	return n;
}

static void
rot_left(tree, node)
	rb_tree	*tree;
	rb_node	*node;
{
	rb_node *rlink, *parent;

	ASSERT(tree != NULL);
	ASSERT(node != NULL);

	rlink = node->rlink;
	node->rlink = rlink->llink;
	if (rlink->llink != RB_NULL)
		rlink->llink->parent = node;
	parent = node->parent;
	rlink->parent = parent;
	if (parent != RB_NULL) {
		if (parent->llink == node)
			parent->llink = rlink;
		else
			parent->rlink = rlink;
	} else {
		tree->root = rlink;
	}
	rlink->llink = node;
	node->parent = rlink;
}

static void
rot_right(tree, node)
	rb_tree	*tree;
	rb_node	*node;
{
	rb_node *llink, *parent;

	ASSERT(tree != NULL);
	ASSERT(node != NULL);

	llink = node->llink;
	node->llink = llink->rlink;
	if (llink->rlink != RB_NULL)
		llink->rlink->parent = node;
	parent = node->parent;
	llink->parent = parent;
	if (parent != RB_NULL) {
		if (parent->llink == node)
			parent->llink = llink;
		else
			parent->rlink = llink;
	} else {
		tree->root = llink;
	}
	llink->rlink = node;
	node->parent = llink;
}

static rb_node *
node_new(key, dat)
	void *key;
	void *dat;
{
	rb_node *node;

	node = MALLOC(sizeof(*node));
	if (node) {
		node->key = key;
		node->dat = dat;
		node->color = RB_RED;
		node->parent = RB_NULL;
		node->llink = RB_NULL;
		node->rlink = RB_NULL;
	}

	return node;
}

static rb_node *
node_next(node)
	rb_node *node;
{
	rb_node *temp;

	ASSERT(node != NULL);

	if (node->rlink != RB_NULL) {
		for (node = node->rlink; node->llink != RB_NULL; node = node->llink)
			/* void */;
	} else {
		temp = node->parent;
		while (temp != RB_NULL && temp->rlink == node) {
			node = temp;
			temp = temp->parent;
		}
		node = temp;
	}

	return node;
}

static rb_node *
node_prev(node)
	rb_node *node;
{
	rb_node *temp;

	ASSERT(node != NULL);

	if (node->llink != RB_NULL) {
		for (node = node->llink; node->rlink != RB_NULL; node = node->rlink)
			/* void */;
	} else {
		temp = node->parent;
		while (temp != RB_NULL && temp->llink == node) {
			node = temp;
			temp = temp->parent;
		}
		node = temp;
	}

	return node;
}

static rb_node *
node_max(node)
	rb_node *node;
{
	ASSERT(node != NULL);

	while (node->rlink != RB_NULL)
		node = node->rlink;
	return node;
}

static rb_node *
node_min(node)
	rb_node *node;
{
	ASSERT(node != NULL);

	while (node->llink != RB_NULL)
		node = node->llink;
	return node;
}

rb_itor *
rb_itor_new(tree)
	rb_tree *tree;
{
	rb_itor *itor;

	ASSERT(tree != NULL);

	itor = MALLOC(sizeof(*itor));
	if (itor) {
		itor->tree = tree;
		rb_itor_first(itor);
	}
	return itor;
}

dict_itor *
rb_dict_itor_new(tree)
	rb_tree *tree;
{
	dict_itor *itor;

	ASSERT(tree != NULL);

	itor = MALLOC(sizeof(*itor));
	if (itor == NULL)
		return NULL;

	if ((itor->_itor = rb_itor_new(tree)) == NULL) {
		FREE(itor);
		return NULL;
	}

	itor->_destroy = (idestroy_func)rb_itor_destroy;
	itor->_valid = (valid_func)rb_itor_valid;
	itor->_invalid = (invalidate_func)rb_itor_invalidate;
	itor->_next = (next_func)rb_itor_next;
	itor->_prev = (prev_func)rb_itor_prev;
	itor->_nextn = (nextn_func)rb_itor_nextn;
	itor->_prevn = (prevn_func)rb_itor_prevn;
	itor->_first = (first_func)rb_itor_first;
	itor->_last = (last_func)rb_itor_last;
	itor->_search = (isearch_func)rb_itor_search;
	itor->_key = (key_func)rb_itor_key;
	itor->_data = (data_func)rb_itor_data;
	itor->_cdata = (cdata_func)rb_itor_cdata;

	return itor;
}

void
rb_itor_destroy(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	FREE(itor);
}

#define RETVALID(itor)		return itor->node != RB_NULL

int
rb_itor_valid(itor)
	const rb_itor *itor;
{
	ASSERT(itor != NULL);

	RETVALID(itor);
}

void
rb_itor_invalidate(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	itor->node = RB_NULL;
}

int
rb_itor_next(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	if (itor->node == RB_NULL)
		rb_itor_first(itor);
	else
		itor->node = node_next(itor->node);
	RETVALID(itor);
}

int
rb_itor_prev(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	if (itor->node == RB_NULL)
		rb_itor_last(itor);
	else
		itor->node = node_prev(itor->node);
	RETVALID(itor);
}

int
rb_itor_nextn(itor, count)
	rb_itor *itor;
	unsigned count;
{
	ASSERT(itor != NULL);

	if (count) {
		if (itor->node == RB_NULL) {
			rb_itor_first(itor);
			count--;
		}

		while (count-- && itor->node)
			itor->node = node_next(itor->node);
	}

	RETVALID(itor);
}

int
rb_itor_prevn(itor, count)
	rb_itor *itor;
	unsigned count;
{
	ASSERT(itor != NULL);

	if (count) {
		if (itor->node == RB_NULL) {
			rb_itor_last(itor);
			count--;
		}

		while (count-- && itor->node)
			itor->node = node_prev(itor->node);
	}

	RETVALID(itor);
}

int
rb_itor_first(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	if (itor->tree->root == RB_NULL)
		itor->node = RB_NULL;
	else
		itor->node = node_min(itor->tree->root);
	RETVALID(itor);
}

int
rb_itor_last(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	if (itor->tree->root == RB_NULL)
		itor->node = RB_NULL;
	else
		itor->node = node_max(itor->tree->root);
	RETVALID(itor);
}

int
rb_itor_search(itor, key)
	rb_itor		*itor;
	const void	*key;
{
	int rv;
	rb_node *node;
	dict_cmp_func cmp;

	ASSERT(itor != NULL);

	cmp = itor->tree->key_cmp;
	for (node = itor->tree->root; node != RB_NULL;) {
		rv = cmp(key, node->key);
		if (rv == 0)
			break;
		node = rv < 0 ? node->llink : node->rlink;
	}
	itor->node = node;
	RETVALID(itor);
}

const void *
rb_itor_key(itor)
	const rb_itor *itor;
{
	ASSERT(itor != NULL);

	return itor->node != RB_NULL ? itor->node->key : NULL;
}

void *
rb_itor_data(itor)
	rb_itor *itor;
{
	ASSERT(itor != NULL);

	return itor->node != RB_NULL ? itor->node->dat : NULL;
}

const void *
rb_itor_cdata(itor)
	const rb_itor *itor;
{
	ASSERT(itor != NULL);

	return itor->node != RB_NULL ? itor->node->dat : NULL;
}

