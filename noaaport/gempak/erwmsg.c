#include "ulog.h"
#include "noaaportLog.h"

void	er_wmsg ( char *errgrp, int *numerr, char *errstr, int *iret)
{
*iret = 0;

if ( *numerr != 0 )
   nplError("[%s %d] %s",errgrp,*numerr,errstr);
else
   if(ulogIsVerbose()) nplInfo("[%s %d] %s",errgrp,*numerr,errstr);
}
