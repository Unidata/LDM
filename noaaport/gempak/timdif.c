#include <stdio.h>

void ti_mdif ( int idtar1[], int idtar2[], int *nmin, int *iret)
{
int jyear, jday1, jday2;
int iyr1, iyr2;
int i, ndays, nmin1, nmin2;
int ier;


*iret = 0;
*nmin = 0;

ti_itoj ( idtar1, &jyear, &jday1, &ier);
ti_itoj ( idtar2, &jyear, &jday2, &ier);

if ( ( idtar1[0] < 0 ) || ( idtar2[0] < 0 ) )
   {
   *iret = -12;
   return;
   }

iyr1 = idtar1[0];
iyr2 = idtar2[0];

if ( iyr1 < iyr2 )
   {
   for ( i=iyr1; i < iyr2; i++)
      {
      ti_daym ( i, 2, &ndays);
      jday2+= 365;
      if ( ndays == 29 ) jday2++;
      }
   }
else if ( iyr1 > iyr2 )
   {
   for ( i=iyr2; i < iyr1; i++)
      {
      ti_daym ( i, 2, &ndays);
      jday1 += 365;
      if ( ndays == 29 ) jday1++;
      }
   }

nmin1 = jday1 * 1440 + idtar1[3] * 60 + idtar1[4];
nmin2 = jday2 * 1440 + idtar2[3] * 60 + idtar2[4];

*nmin = nmin1 - nmin2;

}
