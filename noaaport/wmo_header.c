/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "wmo_header.h"
#include "noaaportLog.h"

char *levels(int, int, int);
char *k5toa(unsigned char *pds);
const char *s_pds_center(unsigned char center);
const char *s_pds_model(unsigned char center, unsigned char model);
int verf_time(unsigned char *pds, int *year, int *month, int *day, int *hour);

int read_short1(unsigned char *x, short *value);
int wmo_to_gridid ( char *TT, char *AA );

/* void grib2name ( char *data, size_t sz, -*wmo_header_t hdr,*- char *ident );*/

void init_wmo_header(wmo_header_t *hdr)
{
hdr->TT[0] = '\0';
hdr->AA[0] = '\0';
hdr->ii = 0;
hdr->CCCC[0] = '\0';
hdr->PIL[0] = '\0';
hdr->BBB[0] = '\0';
hdr->DDHHMM[0] = '\0';
hdr->model[0] = '\0';
hdr->time.year = 0;
hdr->time.mon = 0;
hdr->time.mday = 0;
hdr->time.hour = 0;
hdr->time.min = 0;
}

static char *get_line(char *pos, int nchar)
{
int i=0;
while((i < nchar) && (pos[i] != '\n')) i++;

if(pos[i] == '\n')
  return (pos + i + 1);
else
  return(NULL);
}


char *get_wstr(char *pos, char *spos, int nchar)
{
int len1,len2;
int i=0,cnt;
while((i < strlen(pos))&&(pos[i] == ' ')) i++;
if(i < strlen(pos))
   {
   len1 = strlen(spos); 
   cnt = 0;
   while(( isalnum(pos[i + cnt]))&&(cnt < nchar))
      {
      strncat(spos,pos+i+cnt,1);
      cnt++;
      }
   len2 = strlen(spos);
   return(pos + i + (len2 - len1));
   }
else
   return (NULL);
}

int wmo_header(char *prod_name, size_t prod_size, char *wmohead, char *wmometa, int *metaoff)
{
char *cpos,*pos;
int cnt=0;
wmo_header_t hdr;
char tpil[11];
int llen,allnum,nonalph;
int ier;
unsigned char model_id,grid_id,center_id;
unsigned char dattim[6],vcordid,level[2];
unsigned char b1, b2, b3, b4;
short icenter_id, tmps;
unsigned int lensec;
int YYYY,vYYYY,vMM,vDD,vHH,vtime;
int gversion;
time_t time1, time2;
struct tm tm1, tm2;

static char wmoid[255];
/*char *s_pds_model(unsigned char, unsigned char);*/

memset(wmoid,0,sizeof(wmoid));

init_wmo_header(&hdr);

cpos = prod_name;
if( (pos = (char *)get_wstr(cpos, hdr.TT, 2)) == 0 )  return(-1);
cpos = pos;
if( (pos = (char *)get_wstr(cpos, hdr.AA, 2)) == 0 )  return(-1);
cpos = pos;
if(isdigit(cpos[0]))
   {
   ier = sscanf(cpos,"%d",&hdr.ii);
   while(isdigit(cpos[0])) cpos++;
   }

if( (pos = (char *)get_wstr(cpos, hdr.CCCC,4)) == 0 )  return(-1);
cpos = pos;
if( (pos = (char *)get_wstr(cpos, hdr.DDHHMM,6)) == 0 )  return(-1);
cpos = pos;

if( (pos = (char *)get_wstr(cpos, hdr.BBB,9)) != 0 )  
   cpos = pos;

memset(tpil,0,sizeof(tpil));
if( (pos = get_line(cpos,30)) == 0 )  
   {
   return(-1);
   }
else
   {
   static char GRIBstr[]="GRIB";
   char testme[4];
   cpos = pos;

   memcpy ( testme, cpos, 4);
   if ( memcmp( testme, GRIBstr, (size_t)4 ) == 0)
      {
      gversion = (int)cpos[7];
      switch ( gversion )
         {
	 case 0:
	 case 1:
            model_id = *((unsigned char *)cpos+13);
            grid_id = *((unsigned char *)cpos+14);
            center_id = *((unsigned char *)cpos+12);

            dattim[0] = *((unsigned char *)cpos+20);
            dattim[1] = *((unsigned char *)cpos+21);
            dattim[2] = *((unsigned char *)cpos+22);
            dattim[3] = *((unsigned char *)cpos+23);
            dattim[4] = *((unsigned char *)cpos+24);
            dattim[5] = *((unsigned char *)cpos+32);
            vcordid = *((unsigned char *)cpos+17);
            level[0] = *((unsigned char *)cpos+18);
            level[1] = *((unsigned char *)cpos+19);

            if(dattim[0] > 0) dattim[5] = dattim[5] - 1; 
            YYYY = (int)dattim[5]*100 + (int)dattim[0];
            vtime = verf_time((unsigned char *)cpos+8,&vYYYY,&vMM,&vDD,&vHH);
   
            tm1.tm_year    = YYYY - 1900; tm1.tm_mon     = dattim[1] - 1;
            tm1.tm_mday    = dattim[2]; tm1.tm_hour    = dattim[3];
            tm1.tm_min     = dattim[4]; tm1.tm_sec     = 0;
            tm1.tm_isdst   = -1; time1 = mktime(&tm1);
   
            tm2.tm_year    = vYYYY - 1900; tm2.tm_mon     = vMM - 1;
            tm2.tm_mday    = vDD; tm2.tm_hour    = vHH;
            tm2.tm_min     = 0; tm2.tm_sec     = 0;
            tm2.tm_isdst   = -1; time2 = mktime(&tm2);

            /*nplDebug("check %d %d %d\0",(int)vcordid,(int)level[0],(int)level[1]);
            nplDebug("levels %s strlen %d\0",levels((int)vcordid,(int)level[0],(int)level[1]),
	      strlen(levels((int)vcordid,(int)level[0],(int)level[1])));*/

            sprintf(wmometa,"grib/%s/%s/#%03d/%04d%02d%02d%02d%02d/F%03d/%s/%s/ \0",
                (char *)s_pds_center(center_id),(char *)s_pds_model(center_id,model_id),
		   grid_id,
                YYYY,dattim[1],dattim[2],dattim[3],dattim[4],(time2 - time1)/3600,
                (char *)k5toa((unsigned char *)cpos+8),
                levels((int)vcordid,(int)level[0],(int)level[1]));

            sprintf(hdr.model,"/m%s\0",(char *)s_pds_model(center_id,model_id));
	    break;
        case 2:
	    *metaoff = (int)(cpos - prod_name);
	    sprintf(wmometa,"grib2/\0");
            /*else
               {
               b1 = (unsigned char) cpos[12];
               b2 = (unsigned char) cpos[13];
               b3 = (unsigned char) cpos[14];
               b4 = (unsigned char) cpos[15];
               lensec = (((((b1 << 8) + b2) << 8) + b3 ) << 8 ) + b4;
               nplError("grib2 length %u\0",lensec);
	       grib2name(cpos, (size_t)lensec, hdr, wmometa);
               }*/
	    break;
         default:
	    sprintf(wmometa,"gribx/\0");
	    break;
         }
      }
   else
      {
      if(strncmp(cpos,"^NMC",4) == 0) cpos+=4;
      if ( isalnum ( cpos[0] ) )
         {
	 pos = get_line(cpos, prod_size - (cpos - prod_name) );
	 if( pos != NULL )
	    {
	    pos--;
	    while ( ( pos > cpos ) && ( pos[0] < ' ' ) ) 
               pos--;
	    llen = pos - cpos + 1;
	    }
	 else
	    llen = 0;
         /* pos will be 1 after the \n character if found  and llen non 0*/
         if ( llen == 6 )
	    {
	    cnt = llen; /* chop off space padding for pil check */
	    while ( ( cnt > 3 ) && ( isspace ( cpos[cnt-1] ) ) ) cnt--;
	    strncat(tpil,cpos,cnt);

	    nonalph = 0; allnum = 1;
            for(cnt = 0;cnt < strlen(tpil);cnt++)
               {
               if (! isdigit(tpil[cnt])) allnum = 0;
               if (! isalnum(tpil[cnt])) nonalph++;
               }
            if((nonalph == 0)&&(allnum == 0)) 
	       {
	       strcpy(hdr.PIL, tpil);
	       }
	    }
         }
      }
   }


sprintf(wmoid+strlen(wmoid),"%s%s%02d %s %s\0",hdr.TT,hdr.AA,hdr.ii,hdr.CCCC,hdr.DDHHMM);

if(hdr.BBB[0] != '\0')
   sprintf(wmoid+strlen(wmoid)," %s\0",hdr.BBB);

if(hdr.PIL[0] != '\0')
   sprintf(wmoid+strlen(wmoid)," /p%s\0",hdr.PIL);

if(hdr.model[0] != '\0')
   sprintf(wmoid+strlen(wmoid)," %s\0",hdr.model);

if((strlen(wmoid) > 128)||(strlen(wmoid) < 1)) 
   {
   nplError("wmoid is bizzare %d\n",strlen(wmoid));
   return(-1);
   }

strcpy(wmohead,wmoid);
return(0);

}
