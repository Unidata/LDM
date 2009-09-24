/*
 * skiplist.h
 *
 * Interface for skiplist.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: skiplist.h,v 1.1.2.1 2009/08/14 14:34:22 steve Exp $
 */

#ifndef _SKIPLIST_H_
#define _SKIPLIST_H_

#include "dict.h"

BEGIN_DECL

struct skiplist;
typedef struct skiplist skiplist;

skiplist *skiplist_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
							dict_del_func dat_del, unsigned maxlinks));
dict	*skiplist_dict_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
								dict_del_func dat_del, unsigned maxlinks));
void	 skiplist_destroy __P((skiplist *list, int del));

int skiplist_insert __P((skiplist *list, void *key, void *dat, int overwrite));
int skiplist_probe __P((skiplist *list, void *key, void **dat));
void *skiplist_search __P((skiplist *list, const void *key));
const void *skiplist_csearch __P((const skiplist *list, const void *key));
int skiplist_remove __P((skiplist *list, const void *key, int del));
void skiplist_empty __P((skiplist *list, int del));
void skiplist_walk __P((skiplist *list, dict_vis_func visit));
unsigned skiplist_count __P((const skiplist *list));
const void *skiplist_min __P((const skiplist *list));
const void *skiplist_max __P((const skiplist *list));

struct skiplist_itor;
typedef struct skiplist_itor skiplist_itor;

skiplist_itor *skiplist_itor_new __P((skiplist *list));
dict_itor *skiplist_dict_itor_new __P((skiplist *list));
void skiplist_itor_destroy __P((skiplist_itor *list));

int skiplist_itor_valid __P((const skiplist_itor *itor));
void skiplist_itor_invalidate __P((skiplist_itor *itor));
int skiplist_itor_next __P((skiplist_itor *itor));
int skiplist_itor_prev __P((skiplist_itor *itor));
int skiplist_itor_nextn __P((skiplist_itor *itor, unsigned count));
int skiplist_itor_prevn __P((skiplist_itor *itor, unsigned count));
int skiplist_itor_first __P((skiplist_itor *itor));
int skiplist_itor_last __P((skiplist_itor *itor));
int skiplist_itor_search __P((skiplist_itor *itor, const void *key));
const void *skiplist_itor_key __P((const skiplist_itor *itor));
void *skiplist_itor_data __P((skiplist_itor *itor));
const void *skiplist_itor_cdata __P((const skiplist_itor *itor));
int skiplist_itor_set_data __P((skiplist_itor *itor, void *dat, int del));
int skiplist_itor_remove __P((skiplist_itor *itor, int del));

END_DECL

#endif /* !_SKIPLIST_H_ */
