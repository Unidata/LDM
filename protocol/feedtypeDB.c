/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: feedtypeDB.c,v 1.1.2.1 2009/08/14 14:56:08 steve Exp $ */

#include "ldmconfig.h"

#ifdef UNIT_TEST
#  undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "ldm.h"
#include "ulog.h"

#include "dict.h"
#include "feedtypeDB.h"         /* to ensure consistency */

typedef struct ListEntry        ListEntry;

struct ListEntry {
    FeedtypeEntry*      entry;
    ListEntry*          next;
};

struct FeedtypeDB {
    ListEntry*  entryList;
    dict*       nameToEntry;
    dict*       valueToEntry;
    feedtypet   orMask;
    int         valueAdded;
    dict*       feedtypesToInteriorEntry;
};

typedef enum {
    MASK_ENTRY,
    LEAF_ENTRY,
    INTERIOR_ENTRY
}                       EntryType;

struct MaskEntry {
    EntryType           type;
    FeedtypeDB*         db;
    char*               name;
    feedtypet           value;
    int                 added;
};

struct LeafEntry {
    EntryType           type;
    FeedtypeDB*         db;
    char*               name;
    feedtypet           value;
    int                 added;
};

struct InteriorEntry {
    EntryType           type;
    FeedtypeDB*         db;
    char*               name;
    feedtypet           value;
    int                 added;
    feedtypet           mask;
    dict*               leafEntries;
};

struct ValueEntry {
    EntryType           type;
    FeedtypeDB*         db;
    char*               name;
    feedtypet           value;
    int                 added;
};

struct FeedtypeEntry {
    EntryType           type;
    FeedtypeDB*         db;
    char*               name;
    feedtypet           value;
    int                 added;
};


/*
 * Indicates if a feedtype value is a bitmask.
 *
 * Arguments:
 *      *db     The feedtype database.
 *      value   The feedtype value in question.
 *
 * Returns:
 *      0       If the feedtype value is not a bitmask.
 *      1       If the feedtype value is a bitmask.
 */
static int
isMask(
    const FeedtypeDB* const     db,
    feedtypet const             value)
{
    return (value & ~db->orMask) == 0;
}


static void el_free(ListEntry* listEntry, int freeEntries);


/*
 * Frees a FeedtypeEntry.  If the FeedtypeEntry is a ValueEntry, then the leaf
 * ValueEntry-s list is freed but the ValueEntry-s it references are NOT.
 */
static void
freeEntry(
    FeedtypeEntry* const        entry)
{
    if(entry) {
        if (entry->name) {
            free(entry->name);
            entry->name = NULL;
        }

        if (MASK_ENTRY != entry->type) {
            ValueEntry* valueEntry = (ValueEntry*)entry;

            if (valueEntry->leafEntries) {
                dict_destroy(valueEntry->leafEntries, 0);
                valueEntry->leafEntries = NULL;
            }
        }

        free(entry);
    }
}


/*
 * Pushes a FeedtypeEntry onto a list.
 *
 * Arguments:
 *      *list           Pointer to head of linked-list.  Set on and only on 
 *                      success.  May be NULL.
 *      *feedtypeEntry  The FeedtypeEntry to be added to the list.
 *
 * Returns:
 *      NULL            Success.  "*feedtypeEntry" added to list.  "*list" 
 *                      modified.
 *      !NULL           Failure: "*list" not modified:
 *                              FDB_SYSTEM_ERROR        Memory allocation 
 *                                                      failure
 */
static ErrorObj*
el_push(
    ListEntry** const           list,
    FeedtypeEntry* const        feedtypeEntry)
{
    ErrorObj*           error;
    ListEntry* const    listEntry = (ListEntry*)malloc(sizeof(listEntry));

    if (!listEntry) {
        error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
            "Couldn't allocate %lu-byte ListEntry: %s",
            (unsigned long)sizeof(ListEntry), strerror(errno));
    }
    else {
        listEntry->entry = feedtypeEntry;
        listEntry->next = *list;
        *list = listEntry;
        error = NULL;
    }

    return error;
}


/*
 * Frees a list of ListEntries-s starting with a given one.
 *
 * Arguments:
 *      listEntry       Pointer to first entry in list.
 *      freeEntries     Whether or not to free the FeedtypeEntries referenced
 *                      by the list.
 */
static void
el_free(
    ListEntry*  listEntry,
    int         freeEntries)
{
    while (listEntry) {
        ListEntry* const next = listEntry->next;

        if (freeEntries) {
            freeEntry(listEntry->entry);
            listEntry->entry = NULL;
        }

        listEntry->next = NULL;

        free(listEntry);

        listEntry = next;
    }
}


/*
 * Compares two feedtypet-s.  This is done in reverse order so that
 * larger feedtypet-s will be encountered before smaller ones so that the
 * formatting of a feedtypet will favor names of compound feedtypes over
 * primitive feedtypes.
 *
 * Arguments:
 *      vp1     Pointer to the first feedtypet
 *      vp2     Pointer to the second feedtypet
 *
 * Returns:
 *       1      *vp1 < *vp2
 *       0      *vp1 == *vp2
 *      -1      *vp1 > *vp2
 */
static int
compareValues(
    void* const vp1,
    void* const vp2)
{
    feedtypet const ft1 = *(feedtypet*)vp1;
    feedtypet const ft2 = *(feedtypet*)vp2;

    return ft1 < ft2 ? 1 : ft1 == ft2 ? 0 : -1;
}


/*
 * Adds a feedtype-entry to a feedtype database.  If successful, then the entry
 * will be destroyed when fdb_fee() is invoked on the database.
 *
 * Arguments:
 *      *db             The feedtype database.
 *      *entry          The feedtype-entry to be added to the database.
 *      overwriteName   Whether or not to overwrite the name to which the
 *                      value maps if it already maps to another name
 *
 * Returns:
 *      NULL    Success.  "*entry" was added to "*db".
 *      !NULL   Failure: "*entry" was not added to "*db":
 *                      FDB_NAME_DEFINED        Name already defined with 
 *                                              different feedtype-value
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 */
static ErrorObj*
add(
    FeedtypeDB* const           db,
    FeedtypeEntry* const        entry,
    int                         overwriteName)
{
    ErrorObj*   error;
    int         status;

    assert(db);
    assert(entry);

    status = dict_insert(db->nameToEntry, entry->name, entry, 0);

    if (status == 1) {
        error = ERR_NEW1(FDB_NAME_DEFINED, NULL,
            "Name \"%s\" already defined", entry->name);
    }
    else if (0 > status) {
        error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
            "dict_insert() failure: %s", strerror(errno));
    }
    else {
        /*
         * Mapping from name to entry successfully entered.
         */

        status = dict_insert(db->valueToEntry, 
            &entry->value, entry, overwriteName);

        if (0 > status) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "dict_insert() failure: %s", strerror(errno));
        }
        else {
            error = el_push(&db->entryList, entry);
        }

        if (error) {
            dict_remove(db->nameToEntry, entry->name, 0);
        }
        else {
            entry->added = 1;           /* success */
        }
    }                                   /* added to nameToEntry */

    return error;
}


static int
valueEntriesCmp(
    const void* valueEntry1,
    const void* valueEntry2)
{
    const ValueEntry*   ve1 = (ValueEntry*)valueEntry1;
    const ValueEntry*   ve2 = (ValueEntry*)valueEntry2;

    assert(ve1);
    assert(ve2);

    return
        ve1->value > ve2->value
            ? -1                                /* larger values first */
            : ve1->value == ve2->value
                ? 0
                : 1;
}


static int
leafEntriesCmp(
    const void* dict1,
    const void* dict2)
{
    const dict* entries1 = (dict*)dict1;
    const dict* entries2 = (dict*)dict2;
    int         cmp;
    dict_itor*  itor1;
    dict_itor*  itor2;

    assert(entries1);
    assert(entries2);

    itor1 = dict_itor_new(entries1);
    itor2 = dict_itor_new(entries2);

    if (!itor1 || !itor2) {
        err_log_and_free(
            ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate iterator of ordered-set of ValueEntries: %s",
                strerror(errno)),
            ERR_ERROR);
        cmp = 0;                                /* must return something */
    }
    else {
        ValueEntry*     ve1;
        ValueEntry*     ve2;

        for (cmp = 0;
            NULL != (ve1 = (ValueEntry*)dict_itor_data(itor1)) 
            && NULL != (ve2 = (ValueEntry*)dict_itor_data(itor2)) 
            && 0 == (cmp = valueEntriesCmp(ve1, ve2));
            dict_itor_next(itor1), dict_itor_next(itor2)) {
            ;                                   /* empty */
        }

        if (!cmp && ve1 != ve2) {
            /*
             * Identical subset but one is longer.
             */
            cmp = ve1 ? -1 : 1;                 /* longer one first */
        }

        dict_itor_destroy(itor1);
        dict_itor_destroy(itor2);
    }

    return cmp;
}


static int
feedtypesCmp(
    const ValueEntry*   ve1,
    const ValueEntry*   ve2)
{
    int cmp
    
    if (ve1->mask < ve2->mask) {
        cmp = 1;                        /* larger values first */
    }
    else if (ve1->mask > ve2->mask) {
        cmp = -1;
    }
    else {
        /*
         * Can't decide based on mask-values.  Must use leaf-entries.
         */
        if (NULL == ve1->leafEntries || NULL == ve2->leafEntries) {
            cmp = (NULL == ve1->leafEntries) - (NULL == ve2->leafEntries);
        }
        else {
            cmp = leafEntriesCmp(ve1->leafEntries, ve2->leafEntries);
        }
    }

    return cmp;
}


static ErrorObj*
fdb_get_by_feedtypes(
    const FeedtypeDB* const     db,
    const dict* const           leafEntries,
    const feedtypet             mask,
    feedtypet* const            ft)
{
    ErrorObj*           error = NULL;   /* success */
    unsigned            count;

    assert(db);
    assert(leafEntries);
    assert(ft);

    count = dict_count(leafEntries);;

    if (0 == count) {
        /*
         * The corresponding feedtype can't be a ValueEntry.  Return the mask.
         */
        *ft = mask;
    }
    else if (1 == count) {
        /*
         * A single LeafEntry; the corresponding feedtype should be the 
         * LeafEntry, itself.  Return it if the mask is empty.
         */
        if (mask != NONE) {
            error = ERR_NEW1(FDB_NO_SUCH_ENTRY, NULL,
                "LeafEntry-s don't have non-empty masks (%#x)", mask);
        }
        else {
            dict_itor*  itor = dict_itor_new(leafEntries);

            *ft = ((LeafEntry*)dict_itor_data(itor))->value;

            dict_itor_destroy(itor);
        }
    }
    else {
        /*
         * More than one LeafEntry; the corresponding feedtype should be an
         * InteriorEntry.
         */
        InteriorEntry           key;
        const InteriorEntry*    entry;

        key.leafEntries = leafEntries;
        key.mask = mask;

        entry = (InteriorEntry*)dict_search(db->feedtypesToInteriorEntry, &key);

        if (NULL == entry) {
            error = ERR_NEW(FDB_NO_SUCH_ENTRY, NULL,
                "No InteriorEntry corresponds to the set of LeafEntry-s and "
                "mask");
        }
        else {
            *ft = entry->value;
        }
    }

    return error;
}


static ErrorObj*
copyFeedtypes(
    const ValueEntry* const     ve,
    dict* const                 entries,
    feedtype* const             mask)
{
    ErrorObj*   error = NULL;           /* success */

    if (LEAF_ENTRY == ve->type) {
        if (-1 == dict_insert(entries, ve, ve, 0)) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't insert LeafEntry into ordered-set: %s",
                strerror(errno));
        }
    }
    else {
        const InteriorEntry* const      ie = (InteriorEntry*)ve;
        dict_itor* const                itor = dict_itor_new(ie->leafEntries);

        if (NULL == itor) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate iterator: %s", strerror(errno));
        }
        else {
            const LeafEntry*    le;

            for (; NULL != (le = dict_itor_data(itor)); dict_itor_next(itor)) {
                if (-1 == dict_insert(entries, le, le, 0)) {
                    error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                        "Couldn't insert LeafEntry into ordered-set: %s",
                        strerror(errno));
                    break;
                }
            }

            dict_itor_destroy(itor);
        }                               /* iterator allocated */

        if (!error)
            *mask |= ie->mask;
    }                                   /* InteriorEntry */

    return error;
}


/*
 * Returns
 *      NULL    Success.  *ft is set (perhaps to NONE).
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        System error.
 *                      FDB_NO_SUCH_ENTRY       The difference corresponds to a
 *                                              non-existant ValueEntry.
 */
static ErrorObj*
ve_difference(
    const FeedtypeDB*           db,
    const ValueEntry*           ve1,
    const ValueEntry*           ve2,
    const feedtypet** const     ft)
{
    ErrorObj*   error = NULL;           /* success */

    assert(db);
    assert(ve1);
    assert(ve2);
    assert(ft);

    if (ve1 == ve2) {
        *ft = NONE;
    }
    else if (LEAF_ENTRY == ve1->type) {
        /*
         * "ve1" is a LeafEntry.
         */
        if (LEAF_ENTRY == ve2->type) {
            /*
             * Both ValueEntry-s are LeafEntry-s.
             */
            *ft = NONE;                 /* because ve1 != ve2 */
        }
        else {
            /*
             * "ve1" is a LeafEntry and "ve2" is an InteriorEntry.
             */
            const InteriorEntry*        ie2 = (InteriorEntry*)ve2;

            *ft =
                dict_search(ie2->leafEntries, ve1)
                    ? NONE
                    : ve1->value;
        }
    }                                   /* LeafEntry - x */
    else if (INTERIOR_ENTRY == ve2->type) {
        /*
         * Both ValueEntry-s are InteriorEntry-s.
         */
        const InteriorEntry*    ie1 = (InteriorEntry*)ve1;
        const InteriorEntry*    ie2 = (InteriorEntry*)ve2;
        dict_itor*              itor = dict_itor_new(ie1->leafEntries);

        if (NULL == itor) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate iterator: %s", strerror(errno));
        }
        else {
            dict*       entries = hb_tree_new(valueEntriesCmp, NULL, NULL);

            if (NULL == entries) {
                error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                    "Couldn't allocate ordered-set of LeafEntry-s: %s",
                    strerror(errno));
            }
            else {
                LeafEntry*      le;

                for (; NULL != (le = (LeafEntry*)dict_itor_data(itor));
                        dict_itor_next(itor)) {
                    if (NULL == dict_search(ie2->leafEntries, le)) {
                        if (-1 == dict_insert(entries, le, le, 0)) {
                            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                                "Couldn't add LeafEntry to ordered-set: %s",
                                strerror(errno));
                            break;
                        }
                    }
                }

                if (!error)
                    error = fdb_get_by_feedtypes(db, entries, 
                        ie1->mask & ~ie2->mask, ft);

                dict_destroy(entries, 0);
            }

            dict_itor_destroy(itor);
        }                               /* iterator allocated */
    }                                   /* InteriorEntry - InteriorEntry */
    else {
        /*
         * "ve1" is an InteriorEntry and "ve2" is a LeafEntry.
         */
        const InteriorEntry*    ie1 = (InteriorEntry*)ve1;

        if (NULL == dict_search(ie1->leafEntries, ve2)) {
            /*
             * "ve1" doesn't contain "ve2".
             */
            *ft = ve1->value;
        }
        else {
            /*
             * "ve1" contains "ve2".
             */
            dict_itor*  itor = dict_itor_new(ie1->leafEntries);

            if (NULL == itor) {
                error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                    "Couldn't allocate iterator: %s", strerror(errno));
            }
            else {
                dict*   entries = hb_tree_new(valueEntriesCmp, NULL, NULL);

                if (NULL == entries) {
                    error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                        "Couldn't allocate ordered-set of LeafEntry-s: %s",
                        strerror(errno));
                }
                else {
                    LeafEntry*  le;

                    for (; NULL != (le = (LeafEntry*)dict_itor_data(itor));
                            dict_itor_next(itor)) {
                        if (le != (LeafEntry*)ve2) {
                            if (-1 == dict_insert(entries, le, le, 0)) {
                                error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                                    "Couldn't add LeafEntry to ordered-set: %s",
                                    strerror(errno));
                                break;
                            }
                        }
                    }

                    if (!error)
                        error =
                            fdb_get_by_feedtypes(db, entries, ie1->mask, ft);

                    dict_destroy(entries, 0);
                }

                dict_itor_destroy(itor);
            }                           /* iterator allocated */
        }                               /* "ve1" contains "ve2" */
    }                                   /* InteriorEntry - LeafEntry */

    return error;
}


/*
 * Returns
 *      NULL    Success.  *result is set (perhaps to NONE).
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        System error.
 *                      FDB_NO_SUCH_ENTRY       The intersection corresponds to
 *                                              a non-existant ValueEntry.
 */
static ErrorObj*
ve_union(
    const FeedtypeDB*   db,
    const ValueEntry*   ve1,
    const ValueEntry*   ve2,
    feedtypet* const    result)
{
    ErrorObj*   error = NULL;           /* default */

    assert(db);
    assert(ve1);
    assert(ve2);
    assert(result);

    if (ve1 == ve2) {
        *result = ve1->value;
    }
    else {
        feedtypet       mask = 0;
        dict*           entries = hb_tree_new(ValueEntriesCmp, NULL, NULL);

        if (NULL == dict) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate ordered-set of LeafEntry-s: %s",
                strerror(errno));
        }
        else {
            error = copyFeedtypes(ve1, entries, &mask);

            if (!error) {
                error = copyFeedtypes(ve2, entries, &mask);

                if (!error)
                    error = fdb_get_by_feedtypes(db, entries, mask, result)
            }

            dict_destroy(entries, 0);
        }                               /* ordered-set allocated */
    }                                   /* ValueEntry-s differ */

    return error;
}


/*
 * Returns
 *      NULL    Success.  *ft is set (perhaps to NONE).
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        System error.
 *                      FDB_NO_SUCH_ENTRY       The intersection corresponds to
 *                                              a non-existant ValueEntry.
 */
static ErrorObj*
ve_intersect(
    const FeedtypeDB*   db,
    const ValueEntry*   ve1,
    const ValueEntry*   ve2,
    feedtypet* const    ft)
{
    ErrorObj*   error = NULL;           /* default */

    assert(db);
    assert(ve1);
    assert(ve2);
    assert(ft);

    if (ve1 == ve2) {
        *ft = ve1->value;
    }
    else if (LEAF_ENTRY == ve1->type && LEAF_ENTRY == ve2->type) {
        /*
         * Both ValueEntry-s are LeafEntry-s.
         */
        *ft = NONE;                     /* because ve1 != ve2 */
    }
    else if (ve1->type != ve2->type) {
        /*
         * One of the ValueEntries is a LeafEntry and the other is an
         * InteriorEntry.
         *
         * Ensure that "ve1" references the InteriorEntry.
         */
        if (INTERIOR_ENTRY != ve1->type) {
            const ValueEntry*   ve = ve1;
            ve1 = ve2;
            ve2 = ve;
        }

        *ft =
            NULL == dict_search(((InteriorEntry*)ve1)->leafEntries, ve2)
                ? NONE
                : ve2->value;
    }
    else {
        /*
         * Both ValueEntry-s are InteriorEntry-s.
         */
        InteriorEntry*  ie1 = (InteriorEntry*)ve1;
        InteriorEntry*  ie2 = (InteriorEntry*)ve2;
        dict_itor*      itor1;

        /*
         * Ensure that "ie1" references the InteriorEntry with the fewest
         * LeafEntry-s.
         */
        if (dict_count(ie1->leafEntries) > dict_count(ie2->leafEntries)) {
            const InteriorEntry*        ie = ie1;

            ie1 = ie2;
            ie2 = ie;
        }

        itor1 = dict_itor_new(ie1->leafEntries);

        if (!itor1) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate iterator of ordered-set of "LeafEntries: %s",
                strerror(errno));
        }
        else {
            dict*       inter = hb_dict_new(valueEntriesCmp, NULL, NULL);

            if (!inter) {
                error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                    "Couldn't allocate ordered-set of LeafEntries: %s",
                    strerror(errno));
            }
            else {
                LeafEntry*      ie;

                for (; NULL != (ie = (ValueEntry*)dict_itor_data(itor1));
                        dict_itor_next(itor1)) {
                    if (dict_search(ie2->leafEntries, ie))
                        dict_insert(inter, ie, ie, 0);
                }

                error =
                    fdb_get_by_feedtypes(db, inter, ie1->mask & ie2->mask, ft);

                dict_destroy(inter, 0);
            }                           /* intersection set allocated */

            dict_itor_destroy(itor1);
        }                               /* iterator allocated */
    }                                   /* both are InteriorEntry-s */

    return error;
}


/*
 * When the two feedtypes don't match and both are InteriorEntry-s, then this
 * function scales as the product of the number of leaf-entries of the 
 * InteriorEntry with the fewest leaf-entries times the logarithm of the 
 * number of leaf-entries of the other InteriorEntry.
 *
 * Returns
 *      NULL    Success.  *ft is set (perhaps to NONE).
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        System error.
 *                      FDB_NO_SUCH_ENTRY       The intersection corresponds to
 *                                              a non-existant ValueEntry.
 */
static ErrorObj*
ve_match(
    const FeedtypeDB*   db,
    const ValueEntry*   ve1,
    const ValueEntry*   ve2,
    int* const          match)
{
    ErrorObj*   error = NULL;           /* default */

    assert(db);
    assert(ve1);
    assert(ve2);
    assert(match);

    if (ve1 == ve2) {
        *match = 1;
    }
    else if (LEAF_ENTRY == ve1->type && LEAF_ENTRY == ve2->type) {
        /*
         * Both ValueEntry-s are LeafEntry-s.
         */
        *match = 0;                     /* because ve1 != ve2 */
    }
    else if (ve1->type != ve2->type) {
        /*
         * One of the ValueEntries is a LeafEntry and the other is an
         * InteriorEntry.
         *
         * Ensure that "ve1" references the InteriorEntry.
         */
        if (INTERIOR_ENTRY != ve1->type) {
            const ValueEntry*   ve = ve1;
            ve1 = ve2;
            ve2 = ve;
        }

        *match = NULL != dict_search(((InteriorEntry*)ve1)->leafEntries, ve2);
    }
    else {
        /*
         * Both ValueEntry-s are InteriorEntry-s.
         */
        InteriorEntry*  ie1 = (InteriorEntry*)ve1;
        InteriorEntry*  ie2 = (InteriorEntry*)ve2;
        dict_itor*      itor1;

        /*
         * Ensure that "ie1" references the InteriorEntry with the fewest
         * LeafEntry-s.
         */
        if (dict_count(ie1->leafEntries) > dict_count(ie2->leafEntries)) {
            const InteriorEntry*        ie = ie1;
            ie1 = ie2;
            ie2 = ie;
        }

        itor1 = dict_itor_new(ie1->leafEntries);

        if (!itor1) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate iterator of ordered-set of "LeafEntries: %s",
                strerror(errno));
        }
        else {
            LeafEntry*  ie;

            for (; NULL != (ie = (ValueEntry*)dict_itor_data(itor1));
                    dict_itor_next(itor1)) {
                if (dict_search(ie2->leafEntries, ie))
                    break;
            }

            *match = NULL != ie;

            dict_itor_destroy(itor1);
        }                               /* iterator allocated */
    }                                   /* both are InteriorEntry-s */

    return error;
}


/*******************************************************************************
 * Begin Public Interface
 ******************************************************************************/


/*
 * Allocates a feedtype database.  The caller should destroy the returned
 * database via fdb_free(FeedtypeDB*).
 *
 * Arguments:
 *      *database       Pointer to feedtype database.  Set on and only on
 *                      success.
 *
 * Returns:
 *      NULL    Success.
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 */
ErrorObj*
fdb_new(
    FeedtypeDB** const  database)
{
    ErrorObj*   error;
    FeedtypeDB* db = (FeedtypeDB*)malloc(sizeof(FeedtypeDB));

    if (!db) {
        error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
            "Couldn't allocate %lu-byte feedtype database: %s",
            (unsigned long)sizeof(FeedtypeDB), strerror(errno));
    }
    else {
        /*
         * New database allocated.
         */
        db->entryList = NULL;
        db->nameToEntry = hb_dict_new((dict_cmp_func)strcmp, NULL, NULL);

        if (!db->nameToEntry) {
            error = ERR_NEW(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate name-to-entry map");
        }
        else {
            /*
             * Name-to-entry map allocated.
             */
            db->valueToEntry =
                hb_dict_new((dict_cmp_func)compareValues, NULL, NULL);

            if (!db->valueToEntry) {
                error = ERR_NEW(FDB_SYSTEM_ERROR, NULL,
                    "Couldn't allocate value-to-entry map");
            }
            else {
                /*
                 * Value-to-entry map allocated.
                 */
                db->feedtypesToInteriorEntry =
                    hb_dict_new((dict_cmp_func)feedtypesCmp, NULL, NULL);

                if (!db->feedtypesToInteriorEntry) {
                    error = ERR_NEW(FDB_SYSTEM_ERROR, NULL,
                        "Couldn't allocate map from feedtypes to ValueEntry");
                }
                else {
                    db->orMask = 0;
                    db->valueAdded = 0;
                    error = NULL;
                }

                if (error)
                    dict_destroy(db->valueToEntry, 0);
            }

            if (error) {
                dict_destroy(db->nameToEntry, 0);
                db->nameToEntry = NULL;
            }
        }

        if (error)
            free(db);
    }

    if (!error)
        *database = db;

    return error;
}


/*
 * Adds a single-bit feedtype entry to the database.  This function must not be
 * invoked after ve_new().
 *
 * Arguments:
 *      *db             The feedtype database
 *      name            The name of the feedtype.  Copied.
 *      bit             The (origin 0) index of the bit
 *      overwriteName   Whether or not to overwrite the name to which the
 *                      bit maps if it already maps to a name
 *
 * Returns:
 *      NULL    Success
 *      !NULL   Failure:
 *                      FDB_INVOCATION_ORDER    ve_new() was invoked
 *                      FDB_NAME_DEFINED        Name already defined
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 *                      FDB_INVALID_VALUE       Invalid bit-index
 */
ErrorObj*
fdb_add_bit(
    FeedtypeDB* const   db,
    const char* const   name,
    unsigned const      bit,
    int                 overwriteName)
{
    ErrorObj*   error;

    if (db->valueAdded) {
        error = ERR_NEW(FDB_INVOCATION_ORDER, NULL,
            "ve_new() already invoked on database");
    }
    else {
        if (bit > 31) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "Invalid bit-index (%d) for bit-entry \"%s\"", bit, name);
        }
        else {
            MaskEntry*  entry;

            error = me_new(db, name, &entry);

            if (!error) {
                entry->value = (feedtypet)1 << bit;

                error = add(db, (FeedtypeEntry*)entry, overwriteName);

                if (error) {
                    freeEntry((FeedtypeEntry*)entry);
                }
                else {
                    db->orMask |= entry->value;
                }
            }                           /* MaskEntry allocated */
        }                               /* valid bit-index */
    }                                   /* no ValueEntry-s yet added */

    return error;
}


/*
 * Returns an empty MaskEntry object.  Destruction of the returned MaskEntry 
 * (via me_free()) is the caller's responsibility until me_add() is 
 * invoked on the object.
 *
 * Arguments:
 *      *db     The feedtype database of which the returned MaskEntry will 
 *              become a member.
 *      name    The name of the feedtype.  Copied.
 *      *mask   Pointer to created entry.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.  "*mask" is set to non-NULL.
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 *                      FDB_NAME_DEFINED        Name already maps to a
 *                                              FeedtypeEntry
 */
ErrorObj*
me_new(
    FeedtypeDB* const   db,
    const char* const   name,
    MaskEntry** const   mask)
{
    ErrorObj*   error = NULL;                   /* success */

    assert(db);
    assert(name);
    assert(mask);

    if (fdb_get_by_name(db, name)) {
        error = ERR_NEW1(FDB_NAME_DEFINED, NULL,
            "Feedtype \"%s\" already maps to a FeedtypeEntry", name);
    }
    else {
        MaskEntry*      entry = (MaskEntry*)malloc(sizeof(MaskEntry));

        if (!entry) {
            error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate %lu-byte MaskEntry: %s",
                (unsigned long)sizeof(MaskEntry), strerror(errno));
        }
        else {
            entry->db = db;
            entry->value = 0;
            entry->added = 0;
            entry->type = MASK_ENTRY;
            entry->name = strdup(name);         /* defensive copy */

            if (!entry->name) {
                error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
                    "Couldn't copy name \"%s\": %s", name, strerror(errno));
            }
            else {
                *mask = entry;                  /* success */
            }

            if (error)
                free(entry);
        }                                       /* entry allocated */
    }                                           /* name not already defined */

    return error;
}


/*
 * Frees a MaskEntry.  If me_add() has already been invoked on the 
 * MaskEntry, then this function does nothing.
 *
 * Arguments:
 *      me      Pointer to the MaskEntry to be freed.  May be NULL.
 */
void
me_free(
    MaskEntry*  me)
{
    if (me && !me->added)
        freeEntry((FeedtypeEntry*)me);
}


/*
 * Includes the bitmask feedtype of a MaskEntry in a MaskEntry.
 *
 * Arguments:
 *      *mask   The MaskEntry in which to include the bitmask feedtype of the
 *              MaskEntry object corresponding to "name".
 *      name    The name of the MaskEntry object whose bitmask feedtype is to be
 *              included in "*mask"
 * Returns:
 *      NULL    Success: the bitmask feedtype of the MaskEntry object
 *              corresponding to "name" has been included in "*mask".
 *      !NULL   Failure:
 *                      FDB_INVALID_NAME        There is no FeedtypeEntry
 *                                              corresponding to "name"
 *                      FDB_INVALID_VALUE       The FeedtypeEntry corresponding
 *                                              to "name" is not a MaskEntry
 *                      FDB_INVOCATION_ORDER    me_add() has been invoked
 *                                              on "*mask"
 */
ErrorObj*
me_include(
    MaskEntry* const    mask,
    const char* const   name)
{
    ErrorObj*                   error;
    const FeedtypeEntry*        entry;

    assert(mask);
    assert(name);

    if (mask->added) {
        error = ERR_NEW1(FDB_INVOCATION_ORDER, NULL,
            "MaskEntry \"%s\" has been added to a database and can't be "
                "modified",
            mask->name);
    }
    else {
        entry = (const FeedtypeEntry*)dict_search(mask->db->nameToEntry, name);

        if (!entry) {
            error = ERR_NEW1(FDB_INVALID_NAME, NULL,
                "No feedtype corresponding to \"%s\"", name);
        }
        else if (MASK_ENTRY != entry->type) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "\"%s\" doesn't correspond to a MaskEntry and can't be "
                "added to MaskEntry \"%s\"", name, mask->name);
        }
        else {
            mask->value |= entry->value;
            error = NULL;
        }
    }

    return error;
}


/*
 * Excludes the bitmask feedtype of a MaskEntry from a MaskEntry.
 *
 * Arguments:
 *      *mask   The MaskEntry in which to exclude the bitmask feedtype of the
 *              MaskEntry object corresponding to "name".
 *      name    The name of the MaskEntry object whose bitmask feedtype is to be
 *              included in "*mask"
 * Returns:
 *      NULL    Success: the bitmask feedtype of the MaskEntry object
 *              corresponding to "name" has been excluded from "*mask".
 *      !NULL   Failure:
 *                      FDB_INVALID_NAME        There is no FeedtypeEntry
 *                                              corresponding to "name"
 *                      FDB_INVALID_VALUE       The FeedtypeEntry corresponding
 *                                              to "name" is not a MaskEntry
 */
ErrorObj*
me_exclude(
    MaskEntry* const    mask,
    const char* const   name)
{
    ErrorObj*                   error;
    const FeedtypeEntry*        entry;

    assert(mask);
    assert(name);

    if (mask->added) {
        error = ERR_NEW1(FDB_INVOCATION_ORDER, NULL,
            "MaskEntry \"%s\" has been added to a database and can't be "
                "modified",
            mask->name);
    }
    else {
        entry = (const FeedtypeEntry*)dict_search(mask->db->nameToEntry, name);

        if (!entry) {
            error = ERR_NEW1(FDB_INVALID_NAME, NULL,
                "No feedtype corresponding to \"%s\"", name);
        }
        else if (MASK_ENTRY != entry->type) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "\"%s\" doesn't correspond to a MaskEntry and can't be "
                "added to MaskEntry \"%s\"", name, mask->name);
        }
        else {
            mask->value &= ~entry->value;
            error = NULL;
        }
    }

    return error;
}


/*
 * Adds a MaskEntry to a feedtype database.  If successful, then the entry
 * will be destroyed when fdb_fee() is invoked on the database.
 *
 * Arguments:
 *      *entry          The MaskEntry to be added to the database.
 *      overwriteName   Whether or not to overwrite the name to which the
 *                      value maps if it already maps to another name
 *
 * Returns:
 *      NULL    Success.  "*entry" was added to the database.
 *      !NULL   Failure: "*entry" was not added to the database:
 *                      FDB_NAME_DEFINED        Name already defined with 
 *                                              different feedtype-value
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 *                      FDB_INVOCATION_ORDER    me_add() has already been
 *                                              invoked on "*mask"
 */
ErrorObj*
me_add(
    MaskEntry* const    entry,
    int                 overwriteName)
{
    ErrorObj*           error;

    if (entry->added) {
        error = ERR_NEW1(FDB_INVOCATION_ORDER, NULL,
            "MaskEntry \"%s\" has already been added to a database",
            entry->name);
    }
    else if (0 == entry->value) {
        error = ERR_NEW1(FDB_INVOCATION_ORDER, NULL,
            "MaskEntry \"%s\" has no bits set", entry->name);
    }
    else {
        error = add(entry->db, (FeedtypeEntry*)entry, overwriteName);
    }

    return error;
}


/*
 * Returns an empty value-based (i.e., non-bitmask) feedtype.  On success,
 * the only functions that can be invoked on the returned ValueEntry are
 * ve_include(), ve_exclude(), and ve_add().  Until ve_add() is invoked on the
 * returned ValueEntry, the ValueEntry is not part of the database so that
 * functions like fdb_intersect() can't be invoked on it's numeric valueand the
 * name of the ValueEntry can't be used in functions like ve_include().  Until
 * ve_add() is invoked, the caller is responsible for freeing the returned
 * ValueEntry via ve_free().
 *
 * Arguments:
 *      *db             The feedtype database of which the returned
 *                      ValueEntry will become a member.
 *      name            The name of the feedtype.  Copied.
 *      value           The feedtype value
 *      overwriteName   Whether or not to overwrite the name to which "value"
 *                      maps if it already maps to a name
 *      *valueEntry     Pointer to created ValueEntry.  Set on and only on 
 *                      success.
 *
 * Returns:
 *      NULL    Success
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 *                      FDB_VALUE_DEFINED       Value already defined
 *                      FDB_INVALID_VALUE       "value" is a bitmask
 *                      FDB_NAME_DEFINED        Name already maps to a
 *                                              FeedtypeEntry
 */
ErrorObj*
ve_new(
    FeedtypeDB* const   db,
    const char* const   name,
    feedtypet           value,
    ValueEntry** const  valueEntry)
{
    ErrorObj*   error = NULL;                   /* success */

    assert(db);
    assert(name);
    assert(valueEntry);

    if (fdb_get_by_name(db, name)) {
        error = ERR_NEW1(FDB_NAME_DEFINED, NULL,
            "Feedtype \"%s\" already maps to a FeedtypeEntry", name);
    }
    else {
        ValueEntry*     entry = (ValueEntry*)malloc(sizeof(ValueEntry));

        if (!entry) {
            error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
                "Couldn't allocate %lu-byte ValueEntry: %s",
                (unsigned long)sizeof(ValueEntry), strerror(errno));
        }
        else {
            entry->type = LEAF_ENTRY;
            entry->value = value;
            entry->db = db;
            entry->mask = 0;
            entry->added = 0;
            entry->leafEntries = NULL;
            entry->name = strdup(name);         /* defensive copy */

            if (!entry->name) {
                error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
                    "Couldn't copy name \"%s\": %s", name, strerror(errno));
            }
            else {
                if (isMask(db, value)) {
                    error = ERR_NEW1(FDB_INVALID_VALUE, NULL,
                        "Value %#x is a bitmask", (unsigned long)value);
                }
                else {
                    *valueEntry = entry;        /* success */
                }                               /* valid value */

                if (error)
                    free(entry->name);
            }                                   /* name duplicated */

            if (error)
                free(entry);
        }                                       /* entry allocated */
    }                                           /* name not already defined */

    return error;
}


/*
 * Frees a ValueEntry.  If ve_add() has alreadby been invoked on the ValueEntry,
 * then this function does nothing.
 *
 * Arguments:
 *      ve      Pointer to the ValueEntry to be freed.  May be NULL.
 */
void
ve_free(
    MaskEntry*  ve)
{
    if (ve && !ve->added)
        freeEntry((FeedtypeEntry*)ve);
}


/*
 * Includes a FeedtypeEntry in a ValueEntry.  This function cannot be invoked
 * on a ValueEntry on which ve_add() has been invoked.
 *
 * Arguments:
 *      *valueEntry     The ValueEntry which to include in the FeedtypeEntry
 *      name            The name of the Feedtype to be included in the 
 *                      ValueEntry
 *
 * Returns:
 *      NULL    Success
 *      !NULL   Failure:
 *                      FDB_INVALID_NAME        There is no FeedtypeEntry
 *                                              corresponding to "name"
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 *                      FDB_INVOCATION_ORDER    ve_add() was already invoked
 *                                              on the ValueEntry
 */
ErrorObj*
ve_include(
    ValueEntry* const   valueEntry,
    const char* const   name)
{
    ErrorObj*           error = NULL;   /* default */

    assert(valueEntry);
    assert(name);

    if (valueEntry->added) {
        error = ERR_NEW(FDB_INVOCATION_ORDER, NULL,
            "ve_add() already invoked on ValueEntry");
    }
    else {
        FeedtypeEntry*  entry =
            (FeedtypeEntry*)dict_search(valueEntry->db->nameToEntry, name);

        if (!entry) {
            error = ERR_NEW1(FDB_INVALID_NAME, NULL,
                "No FeedtypeEntry corresponding to \"%s\"", name);
        }
        else if (MASK_ENTRY == entry->type) {
            valueEntry->mask |= entry->value;
        }
        else if (entry != (FeedtypeEntry*)valueEntry) {
            if (NULL == valueEntry->leafEntries) {
                valueEntry->leafEntries =
                    hb_dict_new(valueEntriesCmp, NULL, NULL);

                if (NULL == valueEntry->leafEntries) {
                    error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                        "Couldn't allocate ordered-set of leaf "
                            "ValueEntries: %s", strerror(errno));
                }
            }

            if (!error) {
                if (NULL == ((ValueEntry*)entry)->leafEntries) {
                    if (dict_insert(valueEntry->leafEntries, entry,
                            entry, 0) == -1) {
                        error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                            "Couldn't insert into leaf ValueEntries: %s",
                            strerror(errno));
                    }
                }
                else {
                    dict_itor*  itor =
                        dict_itor_new(((ValueEntry*)entry)->leafEntries);

                    if (NULL == itor) {
                        error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                            "Couldn't allocate leaf ValueEntries iterator: %s",
                            strerror(errno));
                    }
                    else {
                        ValueEntry*     ve;
                        
                        for (;
                            ve = (ValueEntry*)dict_itor_data(itor);
                            dict_itor_next(itor)) {

                            if (dict_insert(valueEntry->leafEntries,
                                    ve, ve, 0) == -1) {
                                error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                                    "Couldn't insert into leaf "
                                        "ValueEntries: %s",
                                    strerror(errno));
                                break;
                            }
                        }               /* leaf entries loop */

                        dict_itor_destroy(itor);
                    }                   /* iterator allocated */
                }                       /* included entry is non-leaf */
            }                           /* have leafEntries list */
        }                               /* include different ValueEntry */
    }                                   /* ValueEntry not yet added */

    return error;
}


/*
 * Excludes a FeedtypeEntry from a ValueEntry.  This function cannot be invoked
 * on a ValueEntry on which ve_add() has been invoked.
 *
 * Arguments:
 *      *valueEntry     The ValueEntry which to exclude from the FeedtypeEntry
 *      name            The name of the Feedtype to be excluded from the 
 *                      ValueEntry
 *
 * Returns:
 *      NULL    Success
 *      !NULL   Failure:
 *                      FDB_INVALID_NAME        There is no FeedtypeEntry
 *                                              corresponding to "name"
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 */
ErrorObj*
ve_exclude(
    ValueEntry* const   valueEntry,
    const char* const   name)
{
    ErrorObj*           error = NULL;   /* success */

    assert(valueEntry);
    assert(name);
    if (valueEntry->added) {
        error = ERR_NEW(FDB_INVOCATION_ORDER, NULL,
            "ve_add() already invoked on ValueEntry");
    }
    else {
        FeedtypeEntry*  entry =
            (FeedtypeEntry*)dict_search(valueEntry->db->nameToEntry, name);

        if (!entry) {
            error = ERR_NEW1(FDB_INVALID_NAME, NULL,
                "No FeedtypeEntry corresponding to \"%s\"", name);
        }
        else if (MASK_ENTRY == entry->type) {
            valueEntry->mask &= ~entry->value;
        }
        else if (entry != (FeedtypeEntry*)valueEntry) {
            if (NULL != valueEntry->leafEntries) {
                if (NULL == ((ValueEntry*)entry)->leafEntries) {
                    (void)dict_remove(valueEntry->leafEntries, entry, 0);
                }
                else {
                    dict_itor*  itor =
                        dict_itor_new(((ValueEntry*)entry)->leafEntries);

                    if (NULL == itor) {
                        error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                            "Couldn't allocate leaf ValueEntries iterator: %s",
                            strerror(errno));
                    }
                    else {
                        ValueEntry*     ve;
                        
                        for (;
                            ve = (ValueEntry*)dict_itor_data(itor);
                            dict_itor_next(itor)) {

                            (void)dict_remove(valueEntry->leafEntries, ve, 0);
                        }               /* leaf entries loop */

                        dict_itor_destroy(itor);
                    }                   /* iterator allocated */
                }                       /* excluded entry is non-leaf */

                if (!error) {
                    if (0 == dict_count(valueEntry->leafEntries)) {
                        dict_destroy(valueEntry->leafEntries, 0);
                        valueEntry->leafEntries = NULL;
                    }
                }
            }                           /* have leafEntries list */
        }                               /* exclude different ValueEntry */
    }                                   /* ValueEntry not yet added */

    return error;
}


/*
 * Adds a ValueEntry to the database referenced by ve_new().  If this call is
 * successful, then 1) ve_include() and ve_exclude() can no longer be invoked on
 * the ValueEntry; and 2) the ValueEntry will be destroyed when fdb_free() is
 * invoked on the database.  If this function is unsuccessful, then the caller
 * is responsible for freeing the ValueEntry.
 *
 * Arguments:
 *      *valueEntry     The ValueEntry to be committed to the database.
 *      overwriteName   Whether or not to overwrite the name to which the
 *                      ValueEntry's numberic value maps if it's already mapped
 *                      to a name.
 *
 * Returns:
 *      NULL    Success
 *      !NULL   Failure:
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 *                      FDB_INVOCATION_ORDER    ve_add() was already invoked
 *                                              on the ValueEntry
 */
ErrorObj*
ve_add(
    ValueEntry* const   valueEntry,
    const int           overwriteName)
{
    ErrorObj*   error = NULL;                   /* success */

    assert(valueEntry);

    if (valueEntry->added) {
        error = ERR_NEW(FDB_INVOCATION_ORDER, NULL,
            "ve_add() already invoked on ValueEntry");
    }
    else {
        /*
         * Ensure that leaf ValueEntry-s have a NULL "leafEntries" field.
         */
        if (NULL != valueEntry->leafEntries &&
                dict_count(valueEntry->leafEntries) == 0) {
            dict_destroy(valueEntry->leafEntries, 0);
            valueEntry->leafEntries = NULL;
        }

        if (dict_insert(valueEntry->db->feedtypesToInteriorEntry,
                valueEntry, valueEntry, 1) == -1) {
            error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                "Couldn't insert feedtypes into map: %s",
                strerror(errno));
        }
        else {
            error =
                add(valueEntry->db, (FeedtypeEntry*)valueEntry, overwriteName);

            if (error)
                dict_remove(valueEntry->db->feedtypesToInteriorEntry,
                    valueEntry, 0);
        }
    }

    return error;
}


/*
 * Returns the FeedtypeEntry in a feedtype database that corresponds to a name.
 *
 * Arguments:
 *      *db     The feedtype database.
 *      name    The name of the FeedtypeEntry to return
 *
 * Returns:
 *      NULL    The database doesn't contain a FeedtypeEntry corresponding to
 *              the name
 *      !NULL   The FeedtypeEntry corresponding to the name
 */
FeedtypeEntry*
fdb_get_by_name(
    const FeedtypeDB* const     db,
    const char* const           name)
{
    assert(db);
    assert(name);

    return dict_search(db->nameToEntry, name);
}


/*
 * Returns the FeedtypeEntry in a feedtype database that corresponds to a value.
 *
 * Arguments:
 *      *db     The feedtype database.
 *      value   The value of the FeedtypeEntry to return
 *
 * Returns:
 *      NULL    The database doesn't contain a FeedtypeEntry corresponding to
 *              the value
 *      !NULL   The FeedtypeEntry corresponding to the value
 */
FeedtypeEntry*
fdb_get_by_value(
    const FeedtypeDB* const     db,
    feedtypet                   value)
{
    assert(db);

    return dict_search(db->valueToEntry, &value);
}


/*
 * Returns a copy of the name of a FeedtypeEntry.  It is the caller's
 * responsibility to destroy the returned string.
 *
 * Arguments:
 *      entry   The FeedtypeEntry
 *      *name   Pointer to name.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.  "*name" is set.
 *      !NULL   Failure.  "*name" is not set.
 *                      FDB_SYSTEM_ERROR        Memory allocation failure
 */
ErrorObj*
fe_get_name(
    const FeedtypeEntry* const  entry,
    char** const                name)
{
    char*       nam;
    ErrorObj*   error;

    assert(entry);

    nam = strdup(entry->name);

    if (nam) {
        *name = nam;
        error = NULL;
    }
    else {
        error = ERR_NEW2(FDB_SYSTEM_ERROR, NULL,
            "Couldn't copy name \"%s\": %s", entry->name, strerror(errno));
    }

    return error;
}


/*
 * Returns the numeric value of a FeedtypeEntry.
 *
 * Arguments:
 *      entry   The FeedtypeEntry
 *
 * Returns:
 *      The numberic value of the FeedtypeEntry
 */
feedtypet
fe_get_value(
    const FeedtypeEntry* const  entry)
{
    assert(entry);

    return entry->value;
}


/*
 * Returns the numeric feedtype value corresponding to the difference between 
 * two feedtypes represented as numeric values.
 *
 * Arguments:
 *      db      The feedtype database.
 *      ft1     The first numeric feedtype value
 *      ft2     The second numeric feedtype value
 *      *result Pointer to computed intersection.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.  *result is set (perhaps to 0 => empty intersection).
 *      !NULL   Failure:
 *                      FDB_INVALID_VALUE       No feedtype corresponds to ft1 
 *                                              or ft2.
 *                      FDB_SYSTEM_ERROR        Memory allocation error.
 *                      FDB_NO_SUCH_ENTRY       No feedtype corresponds to the
 *                                              requested difference.
 */
ErrorObj*
fdb_difference(
    const FeedtypeDB*   db,
    feedtypet           ft1,
    feedtypet           ft2,
    feedtypet*          difference)
{
    ErrorObj*           error = NULL;   /* success */
    int                 isMask1 = isMask(db, ft1);
    int                 isMask2 = isMask(db, ft2);

    assert(db);
    assert(difference);

    if (isMask1 && isMask2) {
        /*
         * MaskEntry minus MaskEntry.
         */
        *difference = ft1 & ~ft2;
    }
    else if (NONE == ft1) {
        *difference = NONE;
    }
    else if (NONE == ft2) {
        *difference = ft1;
    }
    else if (ft1 == ft2) {
        *difference = NONE;
    }
    else if (isMask1 == isMask2) {
        /*
         * ValueEntry minus ValueEntry.
         */
        const FeedtypeEntry*    fe1 = fdb_get_by_value(db, ft1);
        const FeedtypeEntry*    fe2 = fdb_get_by_value(db, ft2);

        if (NULL == fe1 || NULL == fe2) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "No FeedtypeEntry-s correspond to %#x or %#x",
                ft1, ft2);
        }
        else {
            error = ve_difference((ValueEntry*)fe1, (ValueEntry*)fe2,
                difference);

            if (error)
                error = ERR_NEW2(err_code(error), error,
                    "Couldn't subtract feedtype \"%s\" from "
                    "feedtype \"%s\"", fe2->name, fe1->name);
        }
    }                                   /* ValueEntry - ValueEntry */
    else if (isMask1) {
        /*
         * MaskEntry minus ValueEntry.
         */
        const FeedtypeEntry*    fe2 = fdb_get_by_value(db, ft2);

        if (NULL == fe2) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "No FeedtypeEntry corresponds to %#x", ft2);
        }
        else {
            *difference =
                LEAF_ENTRY == fe2->type
                    ? ft1
                    : ft1 & ~((InteriorEntry*)fe2)->mask;
        }
    }                                   /* MaskEntry - ValueEntry */
    else {
        /*
         * ValueEntry minus MaskEntry.
         */
        const FeedtypeEntry*    fe1 = fdb_get_by_value(db, ft1);

        if (NULL == fe1) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "No FeedtypeEntry corresponds to %#x", ft1);
        }
        else if (LEAF_ENTRY == fe1->type) {
            /*
             * LeafEntry minus MaskEntry.
             */
            *difference = fe1->value;
        }                               /* LeafEntry - MaskEntry */
        else {
            /*
             * InteriorEntry minus MaskEntry.
             */
            const InteriorEntry*        ie1 = (InteriorEntry*)fe1;

            error = fdb_get_by_feedtypes(db, ie1->leafEntries,
                ie1->mask & ~ft2, difference);

            if (error)
                error = ERR_NEW2(err_code(error), error,
                    "Couldn't subtract feedtype %#x from "
                    "feedtype \"%s\"", ft2, fe1->name);
        }                               /* InteriorEntry - MaskEntry */
    }                                   /* ValueEntry - MaskEntry */

    return error;
}


/*
 * Returns the numeric feedtype value corresponding to the union of two
 * feedtypes represented as numeric values.
 *
 * Arguments:
 *      db      The feedtype database.
 *      ft1     The first numeric feedtype value
 *      ft2     The second numeric feedtype value
 *      *result Pointer to computed union.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.  *result is set.
 *      !NULL   Failure:
 *                      FDB_INVALID_VALUE       No feedtype corresponds to ft1 
 *                                              or ft2.
 *                      FDB_SYSTEM_ERROR        Memory allocation error.
 */
ErrorObj*
fdb_union(
    const FeedtypeDB*   db,
    feedtypet           ft1,
    feedtypet           ft2,
    feedtypet*          result)
{
    ErrorObj*           error = NULL;   /* success */
    int                 isMask1 = isMask(db, ft1);
    int                 isMask2 = isMask(db, ft2);

    assert(db);
    assert(result);

    if (isMask1 && isMask2) {
        /*
         * MaskEntry union MaskEntry.
         */
        *result = ft1 & ft2;
    }                                   /* MaskEntry union MaskEntry */
    else if (ft1 == ft2) {
        *result = ft1;
    }
    else if (NONE == ft1) {
        *result = ft2;
    }
    else if (NONE == ft2) {
        *result = ft1;
    }
    else if (isMask1 == isMask2) {
        /*
         * ValueEntry union ValueEntry.
         */
        const FeedtypeEntry*    fe1 = fdb_get_by_value(db, ft1);
        const FeedtypeEntry*    fe2 = fdb_get_by_value(db, ft2);

        if (NULL == fe1 || NULL == fe2) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "No FeedtypeEntry-s correspond to %#x or %#x", ft1, ft2);
        }
        else {
            error = ve_union((ValueEntry*)fe1, (ValueEntry*)fe2, result);
        }
    }                                   /* ValueEntry union ValueEntry */
    else {
        /*
         * One ValueEntry and one MaskEntry.
         *
         * Ensure that ft1 references the ValueEntry and ft2 references the
         * MaskEntry.
         */
        if (isMask1) {
            feedtypet   ft = ft1;
            ft1 = ft2;
            ft2 = ft;
        }

        if (NONE == ft2) {
            *result = ft1;
        }
        else {
            const ValueEntry*   ve1 = fdb_get_by_value(db, ft1);

            if (NULL == ve1) {
                error = ERR_NEW1(FDB_INVALID_VALUE, NULL,
                    "No ValueEntry corresponds to %#x", ft1);
            }
            else if (INTERIOR_ENTRY == ve1->type) {
                /*
                 * InteriorEntry union MaskEntry.
                 */
                const InteriorEntry*    ie1 = (InteriorEntry*)ve1;

                error = fdb_get_by_feedtypes(db, ie1->leafEntries,
                    ie1->mask | ft2, result);

                if (error)
                    error = ERR_NEW2(err_code(error), error,
                        "Couldn't form union of feedtype \"%s\" and "
                        "feedtype %#x", ie1->name, ft2);
            }                           /* InteriorEntry union MaskEntry */
            else {
                /*
                 * LeafEntry union MaskEntry.
                 */
                const LeafEntry*        le1 = (LeafEntry*)ve1;
                dict*                   entries =
                    hb_tree_new(valueEntriesCmp, NULL, NULL);

                if (NULL == entries) {
                    error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                        "Couldn't allocate ordered-set of "
                        "LeafEntry-s: %s", strerror(errno));
                }
                else {
                    if (-1 == dict_insert(entries, le1, le1)) {
                        error = ERR_NEW1(FDB_SYSTEM_ERROR, NULL,
                            "Couldn't insert LeafEntry into "
                            "ordered-set: %s", strerror(errno));
                    }
                    else {
                        error = fdb_get_by_feedtypes(db, entries,
                            ft2, result);
                    }

                    dict_destroy(entries, 0);
                }                       /* ordered-set allocated */
            }                           /* LeafEntry union MaskEntry */
        }                               /* MaskEntry != NONE */
    }                                   /* one ValueEntry, one MaskEntry */

    return error;
}


/*
 * Returns the numeric feedtype value corresponding to the intersection of two
 * feedtypes represented as numeric values.
 *
 * Arguments:
 *      db      The feedtype database.
 *      ft1     The first numeric feedtype value
 *      ft2     The second numeric feedtype value
 *      *result Pointer to computed intersection.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.  *result is set.
 *      !NULL   Failure:
 *                      FDB_INVALID_VALUE       No feedtype corresponds to ft1 
 *                                              or ft2.
 *                      FDB_SYSTEM_ERROR        Memory allocation error.
 *                      FDB_NO_SUCH_ENTRY       No feedtype corresponds to the
 *                                              requested intersection.
 */
ErrorObj*
fdb_intersect(
    const FeedtypeDB*   db,
    feedtypet           ft1,
    feedtypet           ft2,
    feedtypet*          result)
{
    ErrorObj*           error = NULL;   /* success */
    int                 isMask1 = isMask(db, ft1);
    int                 isMask2 = isMask(db, ft2);

    assert(db);
    assert(result);

    if (isMask1 && isMask2) {
        /*
         * MaskEntry intersect MaskEntry.
         */
        *result = ft1 & ft2;
    }                                   /* MaskEntry intersect MaskEntry*/
    else if (ft1 == ft2) {
        *result = ft1;
    }
    else if (NONE == ft1 || NONE == ft2) {
        *result = 0;
    }
    else if (isMask1 == isMask2) {
        /*
         * ValueEntry intersect ValueEntry.
         */
        const FeedtypeEntry*    fe1 = fdb_get_by_value(db, ft1);
        const FeedtypeEntry*    fe2 = fdb_get_by_value(db, ft2);

        if (NULL == fe1 || NULL == fe2) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "No FeedtypeEntry-s correspond to %#x or %#x", ft1, ft2);
        }
        else {
            error = ve_intersect((ValueEntry*)fe1, (ValueEntry*)fe2, result);
        }
    }                                   /* ValueEntry intersect ValueEntry */
    else {
        /*
         * One ValueEntry and one MaskEntry.
         *
        const ValueEntry*       ve1;

        /*
         * Ensure that ft1 references the ValueEntry and ft2 references the
         * MaskEntry.
         */
        if (isMask1) {
            feedtypet   ft = ft1;
            ft1 = ft2;
            ft2 = ft;
        }

        ve1 = (ValueEntry*)fdb_get_by_value(db, ft1);

        if (NULL == ve1) {
            error = ERR_NEW1(FDB_INVALID_VALUE, NULL,
                "No ValueEntry corresponds to %#x", ft1);
        }
        else {
            *result = 
                LEAF_ENTRY == ve1->type
                    ? NONE
                    : ((InteriorEntry*)ve1)->mask & ft2;
        }                               /* ValueEntry found */
    }                                   /* one ValueEntry, one MaskEntry */

    return error;
}


/*
 * Indicates if a particular feedtype matches a general feedtype, where
 * both are represented by their numeric values.
 *
 * If the general and particular feedtypes are both ValueEntry-s and the general
 * feedtype matches the particular feedtype, then this function scales as the
 * product of the number of leaf-entries in the particular feedtype times the
 * logarithm of the numer of leaf-entries in the general feedtype.  If only one
 * feedtype is a ValueEntry, then this function scales as the logarithm of the
 * number of ValueEntry-s.  If both feedtypes are MaskEntry's, then this 
 * function is constant in time.
 *
 * Arguments:
 *      db              The feedtype database.
 *      general         The numeric feedtype value of the general feedtype.
 *      particular      The numeric feedtype value of the particular feedtype.
 *      *result         Pointer to computed result.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.  *result is set.
 *      !NULL   Failure:
 *                      FDB_INVALID_VALUE       No feedtype corresponds to ft1 
 *                                              or ft2.
 *                      FDB_SYSTEM_ERROR        Memory allocation error.
 */
ErrorObj*
fdb_match(
    const FeedtypeDB*   db,
    feedtypet           general,
    feedtypet           particular,
    int* const          result)
{
    ErrorObj*           error = NULL;   /* success */
    int                 isMask1 = isMask(db, general);
    int                 isMask2 = isMask(db, particular);

    if (isMask1 && isMask2) {
        /*
         * MaskEntry match MaskEntry.
         */
        *result = general & particular;
    }
    else if (general == particular) {
        *result = 1;
    }
    else if (NONE == general || NONE == particular) {
        *result = 0;
    }
    else if (isMask1 == isMask2) {
        /*
         * ValueEntry match ValueEntry.
         */
        const ValueEntry*       ve1 = (ValueEntry*)fdb_get_by_value(db, ft1);
        const ValueEntry*       ve2 = (ValueEntry*)fdb_get_by_value(db, ft2);

        if (NULL == ve1 || NULL == ve2) {
            error = ERR_NEW2(FDB_INVALID_VALUE, NULL,
                "No FeedtypeEntry-s correspond to %#x or %#x",
                general, particular);
        }
        else {
            error = ve_match(ve1, ve2, result);
        }
    }                                   /* ValueEntry match ValueEntry */
    else {
        /*
         * One ValueEntry and one MaskEntry.
         *
        const ValueEntry*       ve1;

        /*
         * Ensure that "general" references the ValueEntry and "particular"
         * references the MaskEntry.
         */
        if (isMask1) {
            feedtypet   ft = general;
            general = particular;
            particular = ft;
        }

        ve1 = (ValueEntry*)fdb_get_by_value(db, general);

        if (NULL == ve1) {
            error = ERR_NEW1(FDB_INVALID_VALUE, NULL,
                "No ValueEntry corresponds to %#x", general);
        }
        else {
            *result = 
                LEAF_ENTRY == ve1->type
                    ? 0
                    : ((InteriorEntry*)ve1)->mask & ft2;
        }                               /* ValueEntry found */
    }                                   /* one ValueEntry, one MaskEntry */

    return error;
}


/*
 * Frees a feedtype database.
 *
 * Arguments:
 *      *db     The feedtype database.
 */
void
fdb_free(
    FeedtypeDB* const   db)
{
    assert(db);

    if (db->feedtypesToInteriorEntry) {
        dict_destroy(db->feedtypesToInteriorEntry, 0);
        db->feedtypesToInteriorEntry = NULL;
    }

    if (db->nameToEntry) {
        dict_destroy(db->nameToEntry, 0);
        db->nameToEntry = NULL;
    }

    if (db->valueToEntry) {
        dict_destroy(db->valueToEntry, 0);
        db->valueToEntry = NULL;
    }

    el_free(db->entryList, 1);
    db->entryList = NULL;

    free(db);
}


#ifdef UNIT_TEST

int
main(
    int                 argc,
    char** const        argv)
{
    FeedtypeDB*         db;
    FeedtypeEntry*      entry;
    char*               name;
    MaskEntry*          maskEntry;
    ErrorObj*           error;

    assert(-1 != openulog("feedtypeDB", 0, LOG_LDM, "-"));

    assert(!fdb_new(&db));

    assert(error = fdb_add_bit(db, "badbit", -1, 0));
    assert(err_code(error) == FDB_INVALID_VALUE);
    err_free(error);

    assert(error = fdb_add_bit(db, "badbit", 32, 0));
    assert(err_code(error) == FDB_INVALID_VALUE);
    err_free(error);

    assert(!fdb_add_bit(db, "ft0", 0, 0));

    assert(!fdb_get_by_name(db, "badbit"));

    assert(entry = fdb_get_by_name(db, "ft0"));
    assert(fe_get_value(entry) == 1);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "ft0") == 0);
    free(name);

    assert(!fdb_get_by_value(db, 2));

    assert(entry = fdb_get_by_value(db, 1));
    assert(fe_get_value(entry) == 1);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "ft0") == 0);
    free(name);

    assert(!fdb_add_bit(db, "bit-0", 0, 0));

    assert(!fdb_add_bit(db, "bit-1", 0, 1));

    assert(entry = fdb_get_by_value(db, 1));
    assert(fe_get_value(entry) == 1);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "bit-1") == 0);
    free(name);

    assert(!fdb_add_bit(db, "bit-2", 1, 0));

    assert(!me_new(db, "mask-1", &maskEntry));
    assert(error = me_include(maskEntry, "bad"));
    assert(err_code(error) == FDB_INVALID_NAME);
    err_free(error);
    assert(!me_include(maskEntry, "bit-1"));
    assert(!me_include(maskEntry, "bit-2"));
    assert(!me_add(maskEntry, 0));

    assert(error = me_include(maskEntry, "bit-1"));
    assert(err_code(error) == FDB_INVOCATION_ORDER);
    err_free(error);

    assert(error = me_new(db, "mask-1", &maskEntry));
    assert(err_code(error) == FDB_NAME_DEFINED);
    err_free(error);
    me_free(maskEntry);

    assert(!me_new(db, "mask-2", &maskEntry));
    assert(!me_include(maskEntry, "bit-1"));
    assert(!me_add(maskEntry, 0));

    assert(entry = fdb_get_by_name(db, "mask-1"));
    assert(fe_get_value(entry) == 3);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "mask-1") == 0);
    free(name);

    assert(entry = fdb_get_by_value(db, 3));
    assert(fe_get_value(entry) == 3);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "mask-1") == 0);
    free(name);

    assert(!fdb_add_bit(db, "bit-3", 2, 0));

    assert(!me_new(db, "mask-3", &maskEntry));
    assert(!me_include(maskEntry, "mask-1"));
    assert(!me_include(maskEntry, "bit-3"));
    assert(!me_exclude(maskEntry, "bit-1"));
    assert(!me_add(maskEntry, 0));

    assert(entry = fdb_get_by_name(db, "mask-3"));
    assert(fe_get_value(entry) == 6);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "mask-3") == 0);
    free(name);

    assert(entry = fdb_get_by_value(db, 6));
    assert(fe_get_value(entry) == 6);
    assert(!fe_get_name(entry, &name));
    assert(strcmp(name, "mask-3") == 0);
    free(name);

    fdb_free(db);

    return 0;
}

#endif
