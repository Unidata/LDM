/*
 *   Copyright 2003, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */

/*
 * This module implements feed-values and a database of feed-values.  A feed-
 * value has a non-bitmask-based feedtype, an identifier, a (possibly empty)
 * set of child feed-values, and a (possibly empty) child feed-mask.
 */

#include "config.h"

#include <assert.h>
#include <string.h>
#include <strings.h>

#include "feedmask.h"
#include "feedvalue.h"          /* to ensure consistency */
#include "ldm.h"


/******************************************************************************
 * Begin private portion of module "FeedValue"
 ******************************************************************************/


typedef struct {
    feedtypet ft;       /* numeric value of the feed-value */
    feedtypet mask;     /* numeric value of all contained feed-masks */
    hb_tree*  ftMap;    /* database of contained feed-values */
    char      id[1];    /* identifier of the feed-value */
} FeedValue;


static int          isOpen;
static hb_tree*     globalFtMap;
static hb_tree*     globalIdMap;


/*
 * Compares two feedtypes.
 *
 * Arguments:
 *      p1      Pointer to first feedtype.
 *      p2      Pointer to second feedtype.
 * Returns:
 *      -1      The first feedtype is less than the second.
 *       0      The feedtypes are equal.
 *       1      The first feedtype is greater than the second.
 */
static int
compareFeedtypes(
    const void* const p1,
    const void* const p2)
{
    feedtypet ft1 = *(feedtypet*)p1;
    feedtypet ft2 = *(feedtypet*)p2;

    return
        ft1 < ft2
            ? -1
            : ft1 == ft2
                ? 0
                : 1;
}


/*
 * Adds a child feed-value to an encompassing, parent feed-value.  The
 * child feed-values of the child feed-value are not added to the parent 
 * feed-value.
 *
 * Arguments:
 *      parent          The parent feed-value.
 *      child           The child feed-value to be added to the parent.
 * Returns:
 *      FV_NOMEM        Out of memory.
 */
static FeedValueError
addChild(
    FeedValue* const       parent,
    const FeedValue* const child)
{
    FeedValueError error;
    int            mapWasNull = 0;

    if (!parent->ftMap) {
        parent->ftMap = hb_tree_new(compareFeedtypes, NULL, NULL);

        if (parent->ftMap) {
            mapWasNull = 1;
        }
        else {
            error = FV_NOMEM;
        }
    }

    if (parent->ftMap) {
        if (hb_tree_insert(parent->ftMap, &child->ft, child, /*override=*/0)
            == -1) {

            error = FV_NOMEM;
        }
        else {
            /*
             * Add the child's feed-mask to the parent's feed-mask.
             */
            parent->mask = fm_union(parent->mask, child->mask);
        }
    }

    if (error && mapWasNull) {
        hb_tree_destroy(parent->ftMap, /*free=*/0);
        parent->ftMap = NULL;
    }

    return error;
}


/*
 * Removes a child feed-value from a parent feed-value.
 *
 * Arguments:
 *      parent  The feed-value.
 *      child   The feed-value to be removed.
 */
static void
removeChild(
    FeedValue* const       parent,
    const FeedValue* const child)
{
    if (parent->ftMap) {
        (void)hb_tree_remove(parent->ftMap, &child->ft, /*free=*/0);

        parent->mask = fm_difference(parent->mask, child->mask);

        if (hb_tree_count(parent->ftMap) <= 0) {
            hb_tree_destroy(parent->ftMap, /*free=*/0);
            parent->ftMap = NULL;
        }
    }
}


/*
 * Frees a feed-value.  Child feed-values are not freed.
 *
 * Arguments:
 *      fv      The feed-value or NULL.
 */
static void
freeFeedValue(
    FeedValue* fv)
{
    if (fv) {
        if (fv->ftMap) {
            hb_tree_destroy(fv->ftMap, /*free=*/0);
            fv->ftMap = NULL;
        }

        free(fv);
    }
}


/******************************************************************************
 * Begin public portion of module "FeedValue"
 ******************************************************************************/


/*
 * Initializes this module.
 *
 * Returns:
 *      0               Success.
 *      FV_NOMEM        Out of memory.
 */
FeedValueError
fv_open()
{
    FeedValueError error;

    if (isOpen) {
        error = 0;
    }
    else {
        /*
         * Because the feedtype and identifier are in the FeedValue structure,
         * we never free the keys of the binary search tree.
         */

        globalFtMap = hb_tree_new(compareFeedtypes, NULL, freeFeedValue);

        if (!globalFtMap) {
            error = FV_NOMEM;
        }
        else {
            globalIdMap =
                hb_tree_new((dict_cmp_func)strcasecmp, NULL, freeFeedValue);

            if (!globalIdMap) {
                error = FV_NOMEM;
            }
            else {
                isOpen = 1;
                error = 0;
            }

            if (error)
                hb_tree_destroy(globalFtMap, /*free=*/0);
        }
    }

    return error;
}


/*
 * Returns a new feed-value corresponding to a feedtype and identifier.  The
 * feed-value is added to the database of known feed-values.
 *
 * Arguments:
 *      ft              The feedtype.
 *      id              The identifier.
 *      fv              The returned feed-value.
 * Returns:
 *      0               Success.  The feed-value was created and added.  *fv is
 *                      set.
 *      FV_STATE        The module is in the wrong state.  fv_open() has not 
 *                      been called.
 *      FV_NOMEM        Out of memory.
 *      FV_DUP_IDENT    The identifier was previously used.
 *      FV_DUP_FEEDTYPE The feedtype was previously used.
 */
FeedValueError
fv_new(
    const feedtypet   ft,
    const char* const id,
    FeedValue** const fv)
{
    FeedValueError error;

    assert(ft);
    assert(id);
    assert(fv);

    if (!isOpen) {
        error = FV_STATE;
    }
    else {
        FeedValue*     fvp = (FeedValue*)malloc(sizeof(FeedValue)+strlen(id));

        if (!fvp) {
            error = FV_NOMEM;
        }
        else {
            int status;

            fvp->ft = ft;
            fvp->mask = 0;
            fvp->ftMap = NULL;
            (void)strcpy(fvp->id, id);

            status = hb_tree_insert(globalFtMap, &fvp->ft, fvp, /*override=*/0);

            if (-1 == status) {
                error = FV_NOMEM;
            }
            else if (1 == status) {             /* found feedtype */
                error = FV_DUP_FEEDTYPE;
            }
            else {
                status = hb_tree_insert(globalIdMap, fvp->id, fvp, 0);

                if (-1 == status) {
                    error = FV_NOMEM;
                }
                else if (1 == status) {         /* found identifier */
                    error = FV_DUP_IDENT;
                }
                else {
                    *fv = fvp;
                    error = 0;
                }

                if (error)
                    (void)hb_tree_remove(idTree, &fvp->ft, /*free=*/0);
            }                                   /* globalFtMap modified */

            if (error)
                free(fvp);
        }
    }

    return error;
}


/*
 * Adds a child feed-value to a parent feed-value.  The child feed-values of
 * the child feed-value are also added to the parent feed-value.  This allows
 * feed-values to be composited.
 *
 * Arguments:
 *      parent          The parent feed-value.
 *      child           The child feed-value to be added.
 * Returns:
 *      FV_NOMEM        Out of memory.
 *      FV_CHILD        The "child" feed-value contains the parent.
 */
FeedValueError
fv_addFeedValue(
    FeedValue* const       parent,
    const FeedValue* const child)
{
    FeedValueError error;

    assert(parent);
    assert(child);

    /*
     * Ensure that the child may be contained by the parent.
     */
    if (fv_contains(child, parent->ft)) {
        error = FV_CHILD;
    }
    else {
        /*
         * Add the child feed-value to the parent feed-value.
         */
        if (fv_addChild(parent, child) == 0) {
            if (child->ftMap) {
                /*
                 * Add the feed-values that are children of the child
                 * feed-value to the parent feed-value.
                 */
                hb_itor* itor = hb_itor_new(child->ftMap);

                if (NULL == itor) {
                    error = FV_NOMEM;
                }
                else {
                    for (; hb_itor_valid(itor); hb_itor_next(itor)) {
                        FeedValue* fv = (FeedValue*)hb_itor_data(itor);

                        error = fv_addChild(parent, fv);

                        if (error)
                            break;
                    }

                    if (error) {
                        /*
                         * Back-out of the previous additions.
                         */
                        for (hb_itor_prev(itor);
                            hb_itor_valid(itor);
                            hb_itor_prev(itor)) {

                            error = fv_removeChild(parent,
                                (FeedValue*)hb_itor_data(itor));
                        }
                    }

                    hb_itor_destroy(itor);
                }
            }                                   /* child has children */
        }                                       /* child added */
    }                                           /* containable child */

    return error;
}


/*
 * Adds a child feed-mask to a parent feed-value.
 *
 * Arguments:
 *      parent          The parent feed-value.
 *      child           The child feed-mask to be added.
 */
void
fv_addFeedMask(
    FeedValue* const      parent,
    const FeedMask* const child)
{
    assert(parent);
    assert(child);

    parent->mask = fm_union(parent->mask, fm_getFeedtype(child));
}


/*
 * Returns the feed-value associated with an identifier.
 *
 * Arguments:
 *      id              The identifier.
 *      value           The associated feed-value.
 * Returns:
 *      0               Success.  The identifier was found.  *value is set.
 *      FV_NOT_FOUND    The identifier is not associated wth a feed-value.
 *      FV_STATE        The module is in the wrong state.  fv_open() has
 *                      not been called.
 */
FeedValueError
fv_getByIdentifier(
    const char* const id,
    FeedValue** const value)
{
    FeedValueError error;

    assert(id);
    assert(value);

    if (!isOpen) {
        error = FV_STATE;
    }
    else {
        FeedValue* const fv = hb_tree_search(globalIdMap, id);

        if (!fv) {
            error = FV_NOT_FOUND;
        }
        else {
            *value = fv;
            error = 0;
        }
    }

    return error;
}


/*
 * Returns the feed-value associated with a feedtype.
 *
 * Arguments:
 *      ft              The feedtype.
 *      value           The associated feed-value.
 * Returns:
 *      0               Success.  The feedtype was found.  *value is set.
 *      FV_NOT_FOUND    The feedtype is not associated wth a feed-value.
 *      FV_STATE        The module is in the wrong state.  fv_open() has
 *                      not been called.
 */
FeedValueError
fv_getByFeedtype(
    const feedtype    ft,
    FeedValue** const value)
{
    FeedValueError error;

    assert(value);

    if (!isOpen) {
        error = FV_STATE;
    }
    else {
        FeedValue* const fv = hb_tree_search(globalFtMap, &ft);

        if (!fv) {
            error = FV_NOT_FOUND;
        }
        else {
            *value = fv;
            error = 0;
        }
    }

    return error;
}


/*
 * Indicates if a feedtype is contained by a feed-value.
 *
 * Arguments:
 *      fv              The feed-value.
 *      ft              The feedtype.
 * Returns
 *      0               The feedtype is not contained in the feed-value.
 *      1               The feedtype is contained in the feed-value.
 */
int
fv_contains(
    const FeedValue* const fv,
    const feedtypet        ft)
{
    assert(fv);

    return
        (fv->ft == ft) || 
        (fm_isFeedMask(ft) && fm_contains(fv->mask, ft)) ||
        (hb_tree_csearch(fv->ftMap, &ft) != NULL);
}


/*
 * Frees this module, releasing all resources.
 */
void
fv_close()
{
    if (globalIdTree) {
        hb_tree_destroy(globalIdTree, /*free=*/0);
        globalIdTree = NULL;
    }

    if (globalFtTree) {
        hb_tree_destroy(globalFtTree, /*free=*/1);
        globalFtTree = NULL;
    }

    isOpen = 0;
}
