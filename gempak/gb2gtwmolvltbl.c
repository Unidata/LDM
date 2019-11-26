#include "config.h"

#include "gb2def.h"
#include "proto_gemlib.h"

/*
 * The following is declared here because it isn't declared elsewhere.
 */
extern void ctb_g2rdlvl(char *tbname, G2lvls *lvltbl, int *iret);

void  gb2_gtwmolvltbl( char *wmolvltbl, int iver, G2lvls **g2levtbl,
        const char** const filename, int *iret)
/************************************************************************
 * gb2_gtwmolvltbl							*
 *									*
 * This function reads the WMO GRIB2 level/layer table from             *
 * specified file and returns a structure containing the table          *
 * entries 								*
 *                                                                      *
 * If wmolvltbl is NULL, the default table is read.                     *
 *									*
 * gb2_gtwmolvltbl ( wmolvltbl, iver, g2levtbl, filename, iret  	*
 *									*
 * Input parameters:							*
 *      *wmolvltbl      char            WMO vertical coordinate table   *
 *      iver            int             WMO Table version number        *
 *									*
 * Output parameters:							*
 *	**g2levtbl	G2lvls		struct for level table entries  *
 *	**filename      char            Filename of the table.
 *	*iret		int		Return code			*
 *                                        -29 = Error reading table     *
 **									*
 * Log:									*
 * S. Gilbert/NCEP		 08/2005				*
 ***********************************************************************/
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
    if ( strlen(wmolvltbl) == (size_t)0 ) {
        sprintf( tmpname,"g2vcrdwmo%d.tbl", iver );
    }
    else {
        strncpy( tmpname, wmolvltbl, sizeof(tmpname) )[sizeof(tmpname)-1] = 0;
    }

    /*
     *  Check if table has already been read in. 
     *  If different table, read new one in.
     */
    if ( strcmp( tmpname, currtable ) != 0 ) {
        G2lvls tmptbl = {0,0};

        ctb_g2rdlvl(tmpname, &tmptbl, &ier);
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
