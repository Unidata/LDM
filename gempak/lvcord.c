#include <stdio.h>
#include <string.h>

void lv_cord ( char *vcoord, char *vparm, int *ivert, int *iret, size_t len1, size_t len2)
{
cst_ctoi ( &(vcoord), 1, ivert, iret);
/*printf("lv_cord ivert %d iret %d\n",*ivert,*iret);*/
}
