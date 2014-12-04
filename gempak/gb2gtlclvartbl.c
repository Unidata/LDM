#include "config.h"

#include <stdio.h>
#include "gb2def.h"
#include "proto_gemlib.h"

static char lclcurrtable[LLMXLN];

/*
 * The following is declared here because it isn't declared elsewhere.
 */
extern void ctb_g2rdvar(char *tbname, G2vars_t *vartbl, int *iret);

void  gb2_gtlclvartbl( char *lclvartbl, char *cntr, int lclver,
                       G2vars_t **g2vartbl, int *iret)
/************************************************************************
 * gb2_gtlclvartbl						        *
 *									*
 * This function reads the Local GRIB2 paramter tables from             *
 * specified file and returns a structure containing the table          *
 * entries.                                                             *
 *                                                                      *
 * If lclvartbl is NULL, the default table is read.                     *
 *									*
 * gb2_gtlclvartbl ( lclvartbl, cntr, lclver, g2vartbl, iret )          *
 *									*
 * Input parameters:							*
 *      *lclvartbl      char            Local GRIB2 Parameter table     *
 *                                             filename                 *
 *      *cntr           char            Abbrev for Orig Center          *
 *      lclver            int           Local Table version number      *
 *									*
 * Output parameters:							*
 *      **g2vartbl      G2vars_t        structure for the table entries *
 *	*iret		int		Return code			*
 *                                        -31 = Error reading table     *
 **									*
 * Log:									*
 * S. Gilbert/NCEP		 08/2005				*
 ***********************************************************************/
{

    char tmpname[LLMXLN];
    int  ier;
    static G2vars_t currvartbl={0,0};

/*---------------------------------------------------------------------*/
    *iret = 0;

    /*
     *  Check if user supplied table.  If not, use default.
     */
    if ( strlen(lclvartbl) == (size_t)0 ) {
        sprintf( tmpname,"g2vars%s%d.tbl", cntr, lclver );
    }
    else {
        strcpy( tmpname, lclvartbl );
    }

    /*
     *  Check if table has already been read in. 
     *  If different table, read new one in.
     */
    if ( strcmp( tmpname, lclcurrtable ) != 0 ) {
        if ( currvartbl.info != 0 ) {
            free(currvartbl.info);
            currvartbl.info=0;
            currvartbl.nlines=0;
        }
        ctb_g2rdvar( tmpname, &currvartbl, &ier );
        if ( ier != 0 ) {
            char        ctemp[256];

            currvartbl.nlines=0;
            *iret=-31;
            (void)sprintf(ctemp, "Couldn't open local GRIB2 parameter table: "
                    "\"%s\"", tmpname);
            ER_WMSG("GB",iret,ctemp,&ier,2,strlen(tmpname));
            *g2vartbl = &currvartbl;
            return;
        }
    }
    strcpy( lclcurrtable, tmpname );
    *g2vartbl = &currvartbl;

    /*
     *  Search through table for id.
    gb2_skvar( disc, cat, id, pdtn, &vartbl, g2var, &ier);
    if ( ier == -1 )*iret=-32;
     */

}

const char*
gb2_getlclcurrtable(void)
{
    return lclcurrtable;
}
