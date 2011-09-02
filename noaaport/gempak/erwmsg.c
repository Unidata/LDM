#include <noaaportLog.h>

void er_wmsg(
    const char* const   errgrp,
    const int* const    numerr,
    const char* const   errstr,
    int* const          iret,
    const int           i1,
    const int           i2)
{
    *iret = 0;

    if (*numerr != 0)
        nplError("[%s %d] %s", errgrp, *numerr, errstr);
    else
        nplInfo("[%s %d] %s", errgrp, *numerr, errstr);
}
