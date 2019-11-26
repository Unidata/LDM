#include "config.h"

#include "gb2def.h"
#include <proto_gemlib.h>

/*
 * The following is declared here because it isn't declared elsewhere.
 */
extern void ctb_g2rdlvl(char *tbname, G2lvls *lvltbl, int *iret);

void  gb2_gtlcllvltbl( char *lcllvltbl, char *cntr, int lclver,
                       G2lvls **g2levtbl, const char** filename, int *iret)
/************************************************************************
 * gb2_gtlcllvltbl							*
 *									*
 * This function reads the Local GRIB2 level/layer table from           *
 * specified file and returns a structure containing the table          *
 * entries.                                                             *
 *                                                                      *
 * If lcllvltbl is NULL, the default table is read.                     *
 *									*
 * gb2_gtlcllvltbl ( lcllvltbl, cntr, lclver, g2levtbl, filename, iret) *
 *									*
 * Input parameters:							*
 *      *lcllvltbl      char            Local vertical coordinate table *
 *      *cntr           char            Abbrev for Orig Center          *
 *      lclver          int             Local Table Version number      *
 *									*
 * Output parameters:							*
 *	**g2levtbl	G2lvls		struct for level table entries  *
 *	**filename      char            Filename of the table
 *	*iret		int		Return code			*
 *                                        -29 = Error reading table     *
 **									*
 * Log:									*
 * S. Gilbert/NCEP	08/2005				                *
 * S. Emmerson/Unidata  01/2016  Add `free(tmptbl.info)` before         *
 *                               accepting new table                    *
 ************************************************************************/
{

    char tmpname[LLMXLN];
    int  ier;
    static char currtable[LLMXLN];
    static G2lvls currlvltbl={0,0};

/*---------------------------------------------------------------------*/
    *iret = 0;

    /*
     *  Check if user supplied table.  If not, use default.
     */
    if ( strlen(lcllvltbl) == (size_t)0 ) {
        sprintf( tmpname,"g2vcrd%s%d.tbl", cntr, lclver );
    }
    else {
        strncpy( tmpname, lcllvltbl, sizeof(tmpname) )[sizeof(tmpname)-1] = 0;
    }

    /*
     *  Check if table has already been read in. 
     *  If different table, read new one in.
     */
    if ( strcmp( tmpname, currtable ) != 0 ) {
        G2lvls tmptbl = {0,0};

        ctb_g2rdlvl( tmpname, &tmptbl, &ier );
        if ( ier != 0 ) {
            *iret=-29;
            ER_WMSG("GB",iret,tmpname,&ier,2,strlen(tmpname));
            return;
        }

        free(currlvltbl.info);
        currlvltbl = tmptbl;
        (void)strcpy(currtable, tmpname);
    }

    *filename = currtable;
    *g2levtbl = &currlvltbl;
}
