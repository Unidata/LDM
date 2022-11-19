/*
 *   Copyright 2011, University Corporation for Atmospheric Research
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ldm.h"

#include "gribinsert.h"

#include "grib2.h"
#include "gempak/gb2def.h"

/* wgrib prototypes used */
extern char*    k5toa(unsigned char *pds);
extern char*    levels(int, int, int);
extern int      verf_time(unsigned char *pds, int *year, int *month, int *day, int *hour);

void	grib1name ( char *filename, int seqno,  char *data, char *ident )
{
unsigned char model_id,grid_id,vcordid,center,subcenter;
unsigned char dattim[6],level[2];
int CCYY,YYYY,MM,DD,HH;
time_t time1, time2;
struct tm tm1, tm2;
char prodtmp[255] = {0}, prodid[255],levelstmp[255];
char *pos;
static char datyp[]="grib";
static int isinit=!0;

/* Initialize time zone information */
if(isinit)
   {
   isinit = 0;
   putenv("TZ=UTC0");
   tzset();
   }

	model_id = *((unsigned char *)data+13);
        grid_id = *((unsigned char *)data+14);
	center = *((unsigned char *)data+12);
        subcenter = *((unsigned char *)data+33);

        dattim[0] = *((unsigned char *)data+20);
        dattim[1] = *((unsigned char *)data+21);
        dattim[2] = *((unsigned char *)data+22);
        dattim[3] = *((unsigned char *)data+23);
        dattim[4] = *((unsigned char *)data+24);
        dattim[5] = *((unsigned char *)data+32);

        vcordid = *((unsigned char *)data+17);
        level[0] = *((unsigned char *)data+18);
        level[1] = *((unsigned char *)data+19);

        if(dattim[0] > 0) dattim[5] = dattim[5] - 1; CCYY = dattim[5]*100 + dattim[0];
        (void)verf_time((unsigned char *)data+8,&YYYY,&MM,&DD,&HH);

        tm1.tm_year    = CCYY - 1900; tm1.tm_mon     = dattim[1] - 1;
        tm1.tm_mday    = dattim[2]; tm1.tm_hour    = dattim[3];
        tm1.tm_min     = dattim[4]; tm1.tm_sec     = 0;
        tm1.tm_isdst   = -1; time1 = mktime(&tm1);

        tm2.tm_year    = YYYY - 1900; tm2.tm_mon     = MM - 1;
        tm2.tm_mday    = DD; tm2.tm_hour    = HH;
        tm2.tm_min     = 0; tm2.tm_sec     = 0;
        tm2.tm_isdst   = -1; time2 = mktime(&tm2);

        memset(prodtmp,0,255);
        memset(prodid,0,255);
        memset(levelstmp,0,255);

        sprintf(prodid,"%s",k5toa((unsigned char *)data+8));
        while((pos = strchr(prodid,' ')) != NULL) pos[0] = '_';

        sprintf(levelstmp,"%s",levels((int)vcordid,(int)level[0],(int)level[1]));
        while((pos = strchr(levelstmp,' ')) != NULL) pos[0] = '_';

        snprintf(prodtmp, sizeof(prodtmp)-1, "%s/%s/%s/#%03d/%04d%02d%02d%02d%02d/F%03d/%s/%s! %06d",
                datyp,
                s_pds_center(center,subcenter),s_pds_model(center,model_id),grid_id,
                CCYY,dattim[1],dattim[2],dattim[3],dattim[4],
                (int)(time2 - time1)/3600,
                prodid,
                levelstmp,seqno
                /*(char *)PDStimes(ftim[2],ftim[0],ftim[1],ftim[3]),
                YYYY,MM,DD,HH*/);
        if(strlen(filename) < 253)
                {
                strcpy(ident,filename);
                strncat(ident," !",2);
                strncat(ident,prodtmp,253-strlen(filename));
                }
        else
		{
                strncpy(ident,filename,255);
		ident[255] = '\0';
		}
}

void	grib2name ( char *filename, int seqno,  char *data, size_t sz, char *ident )
{
int i, n, ier, ilen;
int unpack=0, expand=0;
g2int  listsec0[3],listsec1[13],numlocal;
int model_id, grid_id;
char g2name[13], fdats[80];
char prodtmp[255] = {0};
char levelstmp[80];
char prods[128];
static char datyp[]="grib2", slashstr[]="/";
static int tblinit=0;
static char *strptr[5];

Gribmsg curr_g2;
Geminfo curr_gem;

static char g2tables[5][LLMXLN] = {{ 0 }}, *tbllist[5];

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
   ier=g2_getfld( curr_g2.cgrib2, curr_g2.mlength, n+1, unpack, expand,
           &curr_g2.gfld);

   /* initialize strings in geminfo structure */
   memset ( curr_gem.cproj, 0, sizeof(curr_gem.cproj));
   memset ( curr_gem.parm, 0, sizeof(curr_gem.parm));
   memset ( curr_gem.gdattm1, 0, sizeof(curr_gem.gdattm1));
   memset ( curr_gem.gdattm2, 0, sizeof(curr_gem.gdattm2));
   model_id = curr_g2.gfld->ipdtmpl[4];
   grid_id = curr_g2.gfld->griddef;

   gb2_2gem (&curr_g2, &curr_gem, tbllist, &ier);

   if ( ier != 0 )
      {
      sprintf(g2name,"UNK");
      sprintf(levelstmp,"LVL");
      sprintf(fdats,"FHRS");
      }
   else
      {
      sprintf(g2name,"%s",curr_gem.parm);
      cst_rmbl (g2name, g2name, &ilen, &ier );
      if ( n > 0 ) strncat ( prods, ";", 1);
      sprintf(prods+strlen(prods),"%s",g2name);

      strptr[0] = (char *)malloc(12);
      cst_itoc ( &curr_gem.vcord, 1, (char **)(&strptr), &ier);
     
      cst_rxbl (curr_gem.unit, curr_gem.unit, &ilen, &ier); 
      if ( ilen == 0 ) sprintf (curr_gem.unit, "-");
      if ( curr_gem.level[1] == -1 )
	 sprintf(levelstmp,"%d %s %s",curr_gem.level[0],curr_gem.unit,strptr[0]);
      else
         sprintf(levelstmp,"%d-%d %s %s",curr_gem.level[0],curr_gem.level[1],curr_gem.unit,strptr[0]);

      cst_rmbl (curr_gem.gdattm1, curr_gem.gdattm1, &ilen, &ier );
      cst_rmbl (curr_gem.gdattm2, curr_gem.gdattm2, &ilen, &ier );
      if ( ilen > 0 )
         sprintf(fdats,"%s-%s",curr_gem.gdattm1,curr_gem.gdattm2);
      else
         sprintf(fdats,"%s",curr_gem.gdattm1);

      ilen = 1;
      while ( ilen > 0 ) cst_rmst(fdats, slashstr, &ilen, fdats, &ier);

      free(strptr[0]);
      }

   g2_free(curr_g2.gfld);
   curr_g2.gfld = NULL;
   }

snprintf(prodtmp, sizeof(prodtmp)-1, "%s/%s/%s/#%03d/%s/%s/%s! %06d",
		datyp,
                s_pds_center((int)listsec1[0],(int)listsec1[1]),
		s_pds_model((int)listsec1[0],model_id),
		grid_id,
		fdats,
                prods,
                levelstmp,seqno);

if(strlen(filename) < 253)
  {
    strcpy(ident,filename);
    strncat(ident," !",2);
    strncat(ident,prodtmp,253-strlen(filename));
  }
else
  {
    strncpy(ident,filename,255);
    ident[255] = '\0';
  }

return;
}

void	get_gribname ( int gversion, char *data, size_t sz, char *filename, int seqno, char *ident)
{
	
        if(memcmp(data,"GRIB",4) == 0)  
	   {
	   switch ( gversion )
	      {
	      case 0:
	      case 1:
			grib1name ( filename, seqno, data, ident);
			break;
	      case 2:
			grib2name ( filename, seqno, data, sz, ident);
			break;
	      default:
	   		sprintf(ident,"%s !gribx/! %06d",filename,seqno);
	      }
	   }
	else if (memcmp(data,"BUFR",4) == 0)
	   {
	   sprintf(ident,"%s !bufr/! %06d",filename,seqno);
           }
	else
	   {
	   sprintf(ident,"%s !data/! %06d",filename,seqno);
           }

return;
}
