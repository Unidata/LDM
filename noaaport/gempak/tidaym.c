#include <stdio.h>

void ti_daym ( int iyear, int imonth, int *nday )
{
int isleap;

if ( ( ( iyear % 4) == 0 ) && ( ( ( iyear % 100 ) != 0 ) || ( ( iyear % 400 ) == 0 ) ) )
   isleap = 1;
else
   isleap = 0;

switch ( imonth )
   {
   case 2:
	if ( isleap )
	   *nday = 29;
        else
           *nday = 28;
	break;
   case 1:
   case 3:
   case 5:
   case 7:
   case 8:
   case 10:
   case 12:
	   *nday = 31;
	   break;
   default:
	   *nday = 30;
   }

}

