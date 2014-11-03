#include "config.h"

#include <stdio.h>
#include "gb2def.h"
#include "proto_gemlib.h"

void gb2_param ( char *wmovartbl, char *lclvartbl, Gribmsg *cmsg, 
                char *param, int *scal, float *msng, int *iret )
/************************************************************************
 * gb2_param								*
 *									*
 * This routine gets the parameter values                               *
 * from the GRIB2 PDS, and obtains the GEMPAK parameter information     *
 * from the appropriate GRIB2 parameter table.                          *
 *									*
 * If either wmovartbl or lclvartbl are NULL, the default tables are    *
 * read.                                                                *
 *									*
 * gb2_param ( wmovartbl, lclvartbl, cmsg, param, scal, msng, iret )	*
 *									*
 * Input parameters:							*
 *      *wmovartbl      char            WMO parameter table             *
 *      *lclvartbl      char            Local parameter table           *
 *	*cmsg  	    struct Gribmsg      GRIB2  message structure        *
 *									*
 * Output parameters:							*
 *      *param          char            12 character parameter name.    *
 *	*scal		int		scale factor associated with    *
 *                                      current parameter               *
 *	*msng		float		missing value associated with   *
 *                                      current parameter               *
 *	*iret		int		return code			*
 *                                          1 = No gempak param name    *
 *                                              defined for this grid   *
 **									*
 * Log:									*
 * S. Gilbert/NCEP      12/04                                           *
 * S. Gilbert/NCEP      10/05		Fix null character location     *
 * S. Gilbert/NCEP      10/05		Use new routines to read tables *
 ***********************************************************************/
{
    int     ret, ier, disc, cat, id, pdtn, iver, lclver, len;
    char    blanks[13]="            ";
    G2Vinfo  g2var;
    G2vars_t  *g2vartbl;

/*---------------------------------------------------------------------*/

    *iret = 0;
    strncpy( param, "UNKNOWN", 12);

    /* 
     *  Get Parameter information from Parameter table(s).
     */
    iver=cmsg->gfld->idsect[2];
    lclver=cmsg->gfld->idsect[3];
    disc=cmsg->gfld->discipline;
    cat=cmsg->gfld->ipdtmpl[0];
    id=cmsg->gfld->ipdtmpl[1];
    pdtn=cmsg->gfld->ipdtnum;

    if ((iver != 255) &&
        ((disc < 192   || disc == 255  ) && 
         (cat  < 192   || cat  == 255  ) &&
         (id   < 192   || id   == 255  ) &&
         (pdtn < 32768 || pdtn == 65535))) {
       /* 
        *  Get WMO Parameter table.
        */
        gb2_gtwmovartbl(wmovartbl, iver, &g2vartbl, &ier);
    }
    else {
       /* 
        *  Get Local Parameter table.
        */
        gb2_gtlclvartbl(lclvartbl, cmsg->origcntr, lclver, &g2vartbl, &ier);
    }
    if (ier == 0) {
       /* 
        *  Get parameter information from table.
        */
        gb2_skvar(disc, cat, id, pdtn, g2vartbl, &g2var, &ier);

        if (ier != 0) {
            char    ctemp[256];
            int     nbytes = sprintf(ctemp, "Couldn't get parameter info: "
                    "disc=%d, cat=%d, id=%d, pdtn=%d", disc, cat, id, pdtn);

            ER_WMSG("GB", &ier, ctemp, &ret, 2, nbytes);
            *iret = 1;
            return;
        }
    }
    
    /* 
     *  Insert time range period in parameter abbreviation,
     *  if necessary.
     */
    gb2_ctim ( cmsg->tmrange, g2var.gemname );

    /* 
     *  Adjust ensemble information in parameter abbreviation,
     *  if necessary.
     *        NOT DESIRED AT THIS TIME
    gb2_ens ( cmsg->gfld, g2var.gemname );
     */

    /* 
     *  Adjust probability information in parameter abbreviation,
     *  if necessary.
     */
    gb2_prob ( cmsg->gfld, g2var.gemname );

    /*
     * Add generating process information in parameter abbreviation
     * if necessary.
     */
    gb2_proc ( cmsg->gfld, g2var.gemname );

    len = strlen(g2var.gemname);
    strncpy( param, g2var.gemname, 12);
    if ( len > 12 ) param[12]='\0';
    if ( len < 12 )  {     /*  pad gempak parameter name with blanks  */
       strncat( param, blanks, 12-len );
    }
    *scal = g2var.scale;
    *msng = g2var.missing;

}
