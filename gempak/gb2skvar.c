#include "config.h"

#include "gb2def.h"

#include <stdint.h>
#include <stdlib.h>

#define MAKE_KEY(disc, cat, id) ((((disc << 8) | cat) << 8) | id)

/**
 * Compares a parameter-table entry with a target key value. Ignores the product
 * definition template number.
 *
 * @param[in] target  Target
 * @param[in] entry   Entry
 * @retval    -1      Target is less than entry
 * @retval     0      Target is equal to entry
 * @retval     1      Target is greater than entry
 */
static int
compare(const void* const target,
        const void* const entry)
{
    const G2Vinfo* const info = (G2Vinfo*)entry;
    const uint32_t       entryKey = MAKE_KEY(info->discpln, info->categry,
            info->paramtr);
    const uint32_t       targetKey = *(uint32_t*)target;

    return targetKey < entryKey
            ? -1
            : targetKey == entryKey
              ? 0
              : 1;
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

    const uint32_t target = MAKE_KEY(disc, cat, id);
    const G2Vinfo* info = bsearch(&target, vartbl->info, vartbl->nlines,
            sizeof(*vartbl->info), compare);

    if (info == NULL) {
        *iret = -1; // No match
    }
    else {
        /*
         * Found parameter with same discipline, category, and name, but
         * possibly different parameter definition template number (PDTN)
         */
        int delta = pdtn - info->pdtnmbr;

        if (delta == 0) {
            *iret = 0; // Exact match
        }
        else if (delta < 0) {
            for (const G2Vinfo* entry = info - 1;; --entry) {
                if (entry < vartbl->info ||
                        MAKE_KEY(entry->discpln, entry->categry, entry->paramtr)
                        < target) {
                    // Too far
                    *iret = -3; // Entry with greater PDTN
                    break;
                }
                if (entry->pdtnmbr == pdtn) {
                    info = entry;
                    *iret = 0; // Exact match
                    break;
                }
                if (entry->pdtnmbr < pdtn) {
                    info = entry;
                    *iret = -2; // Entry with smaller PDTN
                    break;
                }
                info = entry;
            }
        }
        else {
            for (const G2Vinfo* entry = info + 1;; ++entry) {
                if (entry >= vartbl->info + vartbl->nlines ||
                        MAKE_KEY(entry->discpln, entry->categry, entry->paramtr)
                        > target) {
                    // Too far
                    *iret = -2; // Entry with smaller PDTN
                    break;
                }
                if (entry->pdtnmbr == pdtn) {
                    info = entry;
                    *iret = 0; // Exact match
                    break;
                }
                if (entry->pdtnmbr > pdtn) {
                    *iret = -2; // Entry with smaller PDTN
                    break;
                }
                info = entry;
            }
        }

        g2var->discpln = info->discpln;
        g2var->categry = info->categry;
        g2var->paramtr = info->paramtr;
        g2var->pdtnmbr = info->pdtnmbr;
        g2var->scale = info->scale;
        g2var->missing = info->missing;
        g2var->hzremap = info->hzremap;
        g2var->direction = info->direction;
        strcpy(g2var->name, info->name);
        strcpy(g2var->units, info->units);
        strcpy(g2var->gemname, info->gemname);
    }

#if 0
    for (int n = 0; n < vartbl->nlines; ++n) {

        if ( disc == vartbl->info[n].discpln  &&
             cat  == vartbl->info[n].categry  &&
             id   == vartbl->info[n].paramtr  &&
             pdtn == vartbl->info[n].pdtnmbr ) {

            g2var->discpln=vartbl->info[n].discpln;
            g2var->categry=vartbl->info[n].categry;
            g2var->paramtr=vartbl->info[n].paramtr;
            g2var->pdtnmbr=vartbl->info[n].pdtnmbr;
            strcpy( g2var->name, vartbl->info[n].name );
            strcpy( g2var->units, vartbl->info[n].units );
            strcpy( g2var->gemname, vartbl->info[n].gemname );
            g2var->scale=vartbl->info[n].scale;
            g2var->missing=vartbl->info[n].missing;
            g2var->hzremap=vartbl->info[n].hzremap;
            g2var->direction=vartbl->info[n].direction;
            *iret=0;
            break;
        }
        n++;
    }
#endif

}
