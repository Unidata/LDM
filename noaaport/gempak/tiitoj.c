#include <stdio.h>

void ti_itoj ( int idtarr[], int *jyear, int *jday, int *iret)
{
int iyear, imonth, iday;
int i, ndays;

*iret = 0;

iyear = idtarr[0];
imonth = idtarr[1];
iday = idtarr[2];

*jyear = iyear;
*jday = iday;
if ( imonth > 1 )
   {
   for ( i = 1; i < imonth; i++ )
      {
      ti_daym ( iyear, i, &ndays);
      *jday = *jday + ndays;
      }
   }
}
