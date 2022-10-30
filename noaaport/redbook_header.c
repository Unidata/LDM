/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <log.h>



int read_char1(x, value)
    unsigned char *x;
    char         *value;
{
    *value = x[0];
    return 1;
}

int read_short1(x,value)
    unsigned char *x;
    short         *value;
{

       *value = (x[0] << 8) | x[1];
       return 1;
}


void get_block(bpos,FF,blen,mode,submode)
char *bpos;
char *FF;
int *blen,*mode,*submode;
{
char chvalue;
short sval;

*blen = 0;

(void)read_char1((unsigned char*)bpos,&chvalue);

*FF = (chvalue >> 6) ;
if(*FF == 3)
   {
   /*printf("FF flag 3 mode %d\n",chvalue & 0x3F );*/
   }
(void)read_short1((unsigned char*)bpos,&sval); bpos+=2;
*blen = sval & 0x3FFF;
(void)read_char1((unsigned char*)bpos,&chvalue); bpos++;
*mode = chvalue;
(void)read_char1((unsigned char*)bpos,&chvalue); bpos++;
*submode = chvalue;
/*printf("FF %d len %d mode %d sub %d\n",*FF,*blen,*mode,*submode);*/
}

char cstr1[10],cstr2[7],dstr[14],cpilid[10];

void block_1_1(buf,len)
char *buf;
int len;
{
int i,year,month,day,hour,minute;
char chvalue;
short sval;
  
/* 
for(i=0;i<len;i++) 
   {
   ival = *(buf+i);
   printf("look ival %d\n",ival);
   }
*/

memcpy(cstr1,buf+11,9);
cstr1[9] = '\0';

if(len*2 > 31)
   {
   memcpy(cstr2,buf+26,6);
   cstr2[6] = '\0';
   }
   
i = 20;
(void)read_short1((unsigned char*)buf+i,&sval); i+=2;
year = sval;
(void)read_char1((unsigned char*)buf+i,&chvalue); i++;
month = chvalue;
(void)read_char1((unsigned char*)buf+i,&chvalue); i++;
day = chvalue;
(void)read_char1((unsigned char*)buf+i,&chvalue); i++;
hour = chvalue;
(void)read_char1((unsigned char*)buf+i,&chvalue); i++;
minute = chvalue;
snprintf(dstr, sizeof(dstr), "%04d%02d%02d %02d%02d",
        year,month,day,hour,minute);
dstr[sizeof(dstr)-1] = 0;
}
void block_2_2(buf,len)
char *buf;
int len;
{
int i;

if(len*2 < 21) 
   {
   log_error_q("mode 2 submode 2 short %d\0",len);
   return;
   }
memcpy(cpilid,buf+11,9);
cpilid[9] = '\0';
for(i=0;i<9;i++)
   if(cpilid[i] < 32) cpilid[i] = '\0';

/*
for(i=0;i<len*2;i++) 
   {
   ival = *(buf+i);
   printf("look ival %d",ival);
   if((ival > 31)&&(ival < 127)) printf(" %c",ival);
   printf("\n");
   }
*/
}


/* base time & originating model */
void block_1_6(buf,len)
char *buf;
int len;
{
/*int i,ival;

for(i=0;i<len*2;i++)
   {
   ival = *(buf+i);
   printf("look ival %d",ival);
   if((ival > 31)&&(ival < 127)) printf(" %c",ival);
   printf("\n");
   }
*/
}

/* assume tstr is adequately large (41 characters) */
void redbook_header(char *buf, int nbytes, char *tstr)
{
int i=0,start;
char FF;
int blen,mode,submode;
int DONE=0,bstart=0;

tstr[0] = '\0';
cstr1[0] = '\0';
cstr2[0] = '\0';
dstr[0] = '\0';
cpilid[0] = '\0';

while ( ( i < nbytes ) && (*(buf+i) != '\n') ) i++;
start = i + 1;

while ( ( DONE == 0 ) && ( start < ( nbytes - 4 ) ) ) /* ensure that get_block won't read past nbytes */
   {
   get_block(buf+start,&FF,&blen,&mode,&submode); 

   log_debug("redbook: get_block start %d FF %d blen %d mode %d submode %d\0",start,FF,blen,mode,submode);

   if ( ( FF != 1 ) || ( blen <= 0 ) || ( nbytes < (start + blen*2) ) )
      {
      DONE = 1;
      continue;
      }

   if((mode == 1)&&(submode == 1)) 
      {
      block_1_1(buf+start,blen);
      bstart++;
      }
   else if((mode == 1)&&(submode == 6)) 
      block_1_6(buf+start,blen);
   else if((mode == 2)&&(submode == 2)) 
      {
      block_2_2(buf+start,blen);
      DONE = 1;
      }
   else if((mode == 1)&&(submode == 2)) 
      DONE = 1; 

   /* don't look forever...since the buf isn't that big!!! 
   if((blen <= 0)||(blen > 128)||(start > 512))
      {
      DONE = 1;
      log_error_q("error in redbook header %d %d\0",blen,start);
      }*/
   
  
   start = start + blen*2; 
   }

if(bstart > 0) sprintf(tstr,"%s/%s/%s/%s",cpilid,cstr2,cstr1,dstr);

}
