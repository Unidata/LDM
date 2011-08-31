/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gempak/gb2def.h"

const char *s_pds_center(unsigned char center, unsigned char subcenter);
const char *s_pds_model(unsigned char center, unsigned char model);

void	grib2name ( char *data, size_t sz, char *wmohead, char *ident )
{
int i, n, ier, ilen;
int unpack=0, expand=0;
g2int  listsec0[3],listsec1[13],numlocal;
int model_id, grid_id;
char g2name[13], fdats[80];
char prodtmp[255];
char levelstmp[80];
char prods[128];
static char datyp[]="grib2", slashstr[]="/";
static int tblinit=0;
static char *strptr[5];

Gribmsg curr_g2;
Geminfo curr_gem;

static char g2tables[5][LLMXLN] = { 0 }, *tbllist[5];

curr_g2.cgrib2 = (unsigned char *)data;
curr_g2.mlength = sz;
curr_g2.gfld = NULL;
curr_g2.field_tot = 0;

if ( !tblinit)
    {
      for (i = 0; i < 5; i++)
        tbllist[i] = g2tables[i];
      tblinit = !0;
    }

/* modify below to pass message size as a failsafe check */
/*if ( ( ier = g2_info ( curr_g2.cgrib2, listsec0,listsec1, &(curr_g2.field_tot), &numlocal) ) != 0 ) */
if ( ( ier = g2_info ( curr_g2.cgrib2, curr_g2.mlength, listsec0,listsec1, &(curr_g2.field_tot), &numlocal) ) != 0 ) 
	return;

prods[0] = '\0';
for ( n=0; n < curr_g2.field_tot; n++)
   {
   ier=g2_getfld( curr_g2.cgrib2, n+1, unpack, expand, &curr_g2.gfld);

   /* initialize strings in geminfo structure */
   memset ( curr_gem.cproj, 0, sizeof(curr_gem.cproj));
   memset ( curr_gem.parm, 0, sizeof(curr_gem.parm));
   memset ( curr_gem.gdattm1, 0, sizeof(curr_gem.gdattm1));
   memset ( curr_gem.gdattm2, 0, sizeof(curr_gem.gdattm2));
   model_id = curr_g2.gfld->ipdtmpl[4];

   if ( curr_g2.gfld->griddef == 0 ) 
      grid_id = decode_g2gnum ( curr_g2.gfld );
   else
      grid_id = curr_g2.gfld->griddef;

   gb2_2gem (&curr_g2, &curr_gem, tbllist, &ier);

   if ( ier != 0 )
      {
      sprintf(g2name,"UNK\0");
      sprintf(levelstmp,"LVL\0");
      sprintf(fdats,"FHRS\0");
      }
   else
      {
      sprintf(g2name,"%s\0",curr_gem.parm);
      cst_rmbl (g2name, g2name, &ilen, &ier );
      if ( n > 0 ) strncat ( prods, ";", 1);
      sprintf(prods+strlen(prods),"%s\0",g2name);

      strptr[0] = (char *)malloc(12);
      cst_itoc ( &curr_gem.vcord, 1, (char **)(&strptr), &ier);
     
      cst_rxbl (curr_gem.unit, curr_gem.unit, &ilen, &ier); 
      if ( ilen == 0 ) sprintf (curr_gem.unit, "-\0"); 
      if ( curr_gem.level[1] == -1 )
	 sprintf(levelstmp,"%d %s %s\0",curr_gem.level[0],curr_gem.unit,strptr[0]);
      else
         sprintf(levelstmp,"%d-%d %s %s\0",curr_gem.level[0],curr_gem.level[1],curr_gem.unit,strptr[0]);

      cst_rmbl (curr_gem.gdattm1, curr_gem.gdattm1, &ilen, &ier );
      cst_rmbl (curr_gem.gdattm2, curr_gem.gdattm2, &ilen, &ier );
      if ( ilen > 0 )
         sprintf(fdats,"%s-%s\0",curr_gem.gdattm1,curr_gem.gdattm2);
      else
         sprintf(fdats,"%s\0",curr_gem.gdattm1);

      ilen = 1;
      while ( ilen > 0 ) cst_rmst(fdats, slashstr, &ilen, fdats, &ier);

      free(strptr[0]);
      }

   g2_free(curr_g2.gfld);
   curr_g2.gfld = NULL;
   }

/* see if we can use the wmo header for grid 0 products */
if ( ( grid_id == 0 ) && ( strlen(wmohead) > 11 ) &&
     ( wmohead[7] == 'K' ) && ( wmohead[8] == 'W' ) ) 
   {
   n = wmo_to_gridid ( &wmohead[0], &wmohead[2] );
   if ( n > 0 ) grid_id = n;
   }

sprintf(prodtmp,"%s/%s/%s/#%03d/%s/%s/%s\0",
		datyp,
                s_pds_center((int)listsec1[0],(int)listsec1[1]),
		s_pds_model((int)listsec1[0],model_id),
		grid_id,
		fdats,
                prods,
                levelstmp);

strcpy(ident,prodtmp);

return;
}

