/**
 * Copyright 2012 University Corporation for Atmospheric Research.
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This file is for testing the gmtime(3) function.
 *
 * Created on: Dec 10, 2012
 *     Author: Steven R. Emmerson
 */

#include <time.h>
#include <stdio.h>

int main()
{
    time_t      now;
    struct tm*  timestamp = 0;
    char        buf[256];

    (void)time(&now);

    timestamp = gmtime(&now);

    if (timestamp == 0) {
        perror("gmtime()");
        return 1;
    }

    (void)strftime(buf, sizeof(buf), "%b %d %H:%M:%S ", timestamp);
    puts(buf);

    return 0;
}
