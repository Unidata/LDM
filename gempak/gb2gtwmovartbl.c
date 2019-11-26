#include "config.h"

#include <stdio.h>
#include "gb2def.h"
#include "proto_gemlib.h"

/*
 * The following is declared here because it isn't declared elsewhere.
 */
extern void     ctb_g2rdvar(char *tbname, G2vars_t *vartbl, int *iret);

void     gb2_gtwmovartbl(char *wmovartbl, int iver, G2vars_t **g2vartbl,
                       const char** const restrict filename, int *iret)
/************************************************************************
 * gb2_gtwmovartbl							*
 *									*
 * This function reads the WMO GRIB2 parameter table from               *
 * specified file and returns a structure containing the table          *
 * entries.                                                             *
 *                                                                      *
 * If wmovartbl is NULL, the default table is read.                     *
 *									*
 * gb2_gtwmovartbl ( wmovartbl, iver, g2vartbl, filename, iret )        *
 *									*
 * Input parameters:							*
 *      *wmovartbl      char            WMO GRIB2 Parameter table       *
 *                                             filename                 *
 *      iver            int             WMO Table version number        *
 *									*
 * Output parameters:							*
 *	*g2vartbl	G2vars_t        structure for the table entries *
 *	**filename      char            Name of the file that contains  *
 *	                                the table. May be changed by    *
 *	                                next invocation.                *
 *	*iret		int		Return code			*
 *                                        -31 = Error reading table     *
 **									*
 * Log:									*
 * S. Gilbert/NCEP		 08/2005				*
 * S. Emmerson/UCAR              12/2014
 ***********************************************************************/
{

    char tmpname[LLMXLN];
    int  ier;
    static char currtable[LLMXLN];
    static G2vars_t currvartbl={0,0};

/*---------------------------------------------------------------------*/
    *iret = 0;

    /*
     *  Check if user supplied table.  If not, use default.
     */
    if ( strlen(wmovartbl) == (size_t)0 ) {
        sprintf( tmpname,"g2varswmo%d.tbl", iver );
    }
    else {
        strncpy( tmpname, wmovartbl, sizeof(tmpname) )[sizeof(tmpname)-1] = 0;
    }

    /*
     *  Check if table has already been read in. 
     *  If different table, read new one in.
     */
    if ( strcmp( tmpname, currtable ) != 0 ) {
        static G2vars_t tmptbl = {0, 0};
        ctb_g2rdvar( tmpname, &currvartbl, &ier );
        if ( ier != 0 ) {
            char        ctemp[256];

            currvartbl.nlines=0;
            *iret=-31;
            (void)sprintf(ctemp, "Couldn't open WMO GRIB2 parameter table: "
                    "\"%s\"", tmpname);
            ER_WMSG("GB",iret,ctemp,&ier,2,strlen(tmpname));
            *g2vartbl = &currvartbl;
            return;
        }
        free(currvartbl.info);
        currvartbl = tmptbl;
        strcpy(currtable, tmpname);
    }
    *g2vartbl = &currvartbl;
    *filename = currtable;

    /*
     *  Search through table for id.
    gb2_skvar( disc, cat, id, pdtn, &vartbl, g2var, &ier);
    if ( ier == -1 )*iret=-32;
     */

}
