/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: feedtypeDB.h,v 1.1.2.1 2009/08/14 14:56:08 steve Exp $ */

#ifndef FEEDTYPE_DB_H
#define FEEDTYPE_DB_H

#include <error.h>

typedef struct FeedtypeEntry	FeedtypeEntry;
typedef struct MaskEntry	MaskEntry;
typedef struct ValueEntry	ValueEntry;
typedef struct FeedtypeDB	FeedtypeDB;

typedef enum {
    FDB_SYSTEM_ERROR,
    FDB_NAME_DEFINED,
    FDB_VALUE_DEFINED,
    FDB_INVOCATION_ORDER,
    FDB_INVALID_VALUE,
    FDB_INVALID_NAME,
    FDB_NO_SUCH_ENTRY
}	FeedtypeDbError;

ErrorObj*	fdb_new(
    FeedtypeDB** 		database);
ErrorObj*	fdb_add_bit(
    FeedtypeDB* 		db,
    const char* 		name,
    unsigned 			bit,
    int				overwriteName);
ErrorObj*	me_new(
    FeedtypeDB* 		db,
    const char* 		name,
    MaskEntry** 		mask);
void		me_free(
    MaskEntry*			me);
ErrorObj*	me_include(
    MaskEntry* 			mask,
    const char* 		name);
ErrorObj*	me_exclude(
    MaskEntry* 			mask,
    const char* 		name);
ErrorObj*	me_add(
    MaskEntry* 			entry,
    int				overwriteName);
ErrorObj*	ve_new(
    FeedtypeDB* 		db,
    const char* 		name,
    feedtypet			value,
    ValueEntry** 		valueEntry);
ErrorObj*	ve_include(
    ValueEntry* 		valueEntry,
    const char* 		name);
ErrorObj*	ve_exclude(
    ValueEntry* 		valueEntry,
    const char* 		name);
ErrorObj*	ve_add(
    ValueEntry* 		entry,
    int				overwriteName);
FeedtypeEntry*	fdb_get_by_name(
    const FeedtypeDB* 		db,
    const char* 		name);
FeedtypeEntry*	fdb_get_by_value(
    const FeedtypeDB* 		db,
    feedtypet			value);
ErrorObj*	fe_ge_name(
    const FeedtypeEntry* 	entry,
    char** 			name);
feedtypet	fe_get_value(
    const FeedtypeEntry* 	entry);
ErrorObj*	fdb_contains(
    const FeedtypeDB*		db,
    feedtypet			general,
    feedtypet			particular,
    int* const			contains);
ErrorObj*	fdb_intersect(
    const FeedtypeDB*		db,
    feedtypet			ft1,
    feedtypet			ft2,
    feedtypet*			result);
void		fdb_free(
    FeedtypeDB* 		db);

#endif
