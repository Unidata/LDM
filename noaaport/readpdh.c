/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include "nport.h"

int readpdh(char *buf, pdh_struct *pdh)
{
int iret=0;
unsigned char b1,b2,b3,b4,val;
unsigned long lval;

val = (unsigned char)buf[0];
b1 = (val >> 4);
b2 = (val & 15);
pdh->len = (int)b2 * 4;
pdh->version = (int)b1;

val = (unsigned char)buf[1];
pdh->transtype = val;

b1 = (unsigned char)buf[2];
b2 = (unsigned char)buf[3];
lval =  (b1 << 8) + b2;
pdh->pshlen = lval - pdh->len;

b1 = (unsigned char)buf[4];
b2 = (unsigned char)buf[5];
lval = (b1 << 8) + b2;
pdh->dbno = lval;

b1 = (unsigned char)buf[6];
b2 = (unsigned char)buf[7];
lval = (b1 << 8) + b2;
pdh->dboff = lval;

b1 = (unsigned char)buf[8];
b2 = (unsigned char)buf[9];
lval = (b1 << 8) + b2;
pdh->dbsize = lval;

b1 = (unsigned char)buf[10];
pdh->records_per_block = b1;

b1 = (unsigned char)buf[11];
pdh->blocks_per_record = b1;

b1 = (unsigned char)buf[12];
b2 = (unsigned char)buf[13];
b3 = (unsigned char)buf[14];
b4 = (unsigned char)buf[15];
lval = (((((b1 << 8) + b2) << 8) + b3) << 8) + b4;
pdh->seqno = lval;


return(iret);
}
