#include <stdio.h>
#include <sys/types.h>
#include <string.h>


void tg_itoc ( int *intdtf, char *gdattim, int *iret, size_t len )
{
int iftype, iftime;
char fstr[8], ftype, gtemp[21];

*iret = 0;
gdattim[0] = '\0';

if ( intdtf[0] == 0 && intdtf[1] == 0 && intdtf[2] == 0 )
   {
   *iret = -1;
   return;
   }

sprintf(gtemp,"%08d/%04d",intdtf[0],intdtf[1]);

/*itime[2] = iafgi * 100000 + ihhh * 100 + imm;*/

if ( intdtf[2] > 0 ) /* don't encode negative forecast times! */
   {
   iftype = intdtf[2] / 100000;
   switch ( iftype)
      {
      case 0:
	 ftype = 'A';
	 break;
      case 1:
	 ftype = 'F';
	 break;
      case 2:
	 ftype = 'G';
	 break;
      case 3:
	 ftype = 'I';
	 break;
      default:
	 ftype = 'N';
      }

   iftime = intdtf[2] % 100000;
   if ( intdtf[2] % 100 != 0 ) /*encode minutes*/
      sprintf(fstr,"%c%05d", ftype, iftime);
   else
      sprintf(fstr,"%c%03d", ftype, iftime / 100);
   strcat(gtemp,fstr);
   strncat(gdattim, gtemp, len);
   }

}
