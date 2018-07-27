#include "config.h"

#include "gb2def.h"

#include <stdint.h>
#include <stdlib.h>

#define MAKE_KEY(disc, cat, id) ((((disc << 8) | cat) << 8) | id)

static const G2Vinfo* lastHit;

/**
 * Compares a parameter-table entry with a target key value.
 *
 * @param[in] arg1    Target
 * @param[in] arg2    Entry
 * @retval    -1      Target is less than entry
 * @retval     0      Target is equal to entry
 * @retval     1      Target is greater than entry
 */
static int
compare(const void* const arg1,
        const void* const arg2)
{
    const G2Vinfo* const key = (G2Vinfo*)arg1;
    const G2Vinfo* const elt = (G2Vinfo*)arg2;

    if (key->discpln < elt->discpln) {
        return -1;
    }
    else if (key->discpln > elt->discpln) {
        return 1;
    }
    else if (key->categry < elt->categry) {
        return -1;
    }
    else if (key->categry > elt->categry) {
        return 1;
    }
    else if (key->paramtr < elt->paramtr) {
        return -1;
    }
    else if (key->paramtr > elt->paramtr) {
        return 1;
    }
    else {
        // Found (discipline, category, parameter)
        lastHit = elt;

        return (key->pdtnmbr < elt->pdtnmbr)
                ? -1
                : (key->pdtnmbr > elt->pdtnmbr)
                  ? 1
                  : 0;
    }
}

void  gb2_skvar( int disc, int cat, int id, int pdtn, G2vars_t *vartbl,
                 G2Vinfo *g2var, int *iret)
/************************************************************************
 * gb2_skvar	            						*
 *									*
 * This function searches a GRIB2 parameter table from a                *
 * G2vars_t structure, and returns a structure containing the table     *
 * elements for the entry that matches the three identifying parameter  *
 * numbers and the PDT TEMPLATE number.                                 *
 *                                                                      *
 * gb2_skvar ( disc, cat, id, pdtn, vartbl, g2var, iret )           	*
 *									*
 * Input parameters:							*
 *      disc            int             GRIB2 discipline number         *
 *      cat             int             GRIB2 parameter category        *
 *      id              int             GRIB2 parameter id number       *
 *      pdtn            int             GRIB2 PDT Template number       *
 *	*vartbl		G2vars_t 	structure containing GRIB2      *
 *                                      parameter table entries read    *
 *                                        from a table file.            *
 *									*
 * Output parameters:							*
 *	*g2var		G2Vinfo		structure for parameter table   *
 *                                            entry                     *
 *	*iret		int		Return code			*
 *                                        0 = entry found               *
 *                                       -1 = entry NOT found           *
 *                                       -2 = entry with same
 *                                            discipline, category, &
 *                                            ID but smaller PDTN found *
 *                                       -3 = entry with same
 *                                            discipline, category, &
 *                                            ID but greater PDTN found
 **									*
 * Log:									*
 * S. Gilbert/NCEP		 12/2004				*
 * S. Emmerson/UCAR              7/2018
 *     Replaced linear search with binary search and possible use of entry with
 *     same discipline, category, and parameter ID but different parameter
 *     definition template number
 ***********************************************************************/
{
    /*
     * The following requires that the input table be sorted in increasing order
     * by the tuple (discipline, category, parameter ID, PDTN) with the least to
     * most rapidly varying keys in that order.
     */

    G2Vinfo key;
    key.discpln = disc;
    key.categry = cat;
    key.paramtr = id;
    key.pdtnmbr = pdtn;

    lastHit = NULL;

    const G2Vinfo* entry = bsearch(&key, vartbl->info, vartbl->nlines,
            sizeof(*vartbl->info), compare);

    if (lastHit == NULL) {
        // Didn't find (discipline, category, parameter)
        *iret = -1; // No match
    }
    else {
        // Found (discipline, category, parameter), at least
        if (entry) {
            *iret = 0; // Found exact match
        }
        else {
            // Found entry with different PDTN
            *iret = (lastHit->pdtnmbr < key.pdtnmbr)
                    ? -2
                    : -3;
            entry = lastHit;
        }

        g2var->discpln = entry->discpln;
        g2var->categry = entry->categry;
        g2var->paramtr = entry->paramtr;
        g2var->pdtnmbr = entry->pdtnmbr;
        g2var->scale = entry->scale;
        g2var->missing = entry->missing;
        g2var->hzremap = entry->hzremap;
        g2var->direction = entry->direction;
        strcpy(g2var->name, entry->name);
        strcpy(g2var->units, entry->units);
        strcpy(g2var->gemname, entry->gemname);
    }
}
