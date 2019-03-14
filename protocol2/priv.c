/**
 * This file defines a module that enables and disables root privileges.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 */

#include "config.h"

#include "log.h"
#include "priv.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#if __linux || __linux__ || linux
#include <sys/prctl.h>
#endif

/**
 * Ensures that the process may dump core on a Linux system.
 */
void
ensureDumpable()
{
#if __linux || __linux__ || linux
    /*
     * Workaround non-standard behavior of Linux regarding the creation
     * of a core file for a setuid program owned by root.
     */
    if (-1 == prctl(PR_SET_DUMPABLE, 1, 0, 0, 0))
        log_syserr("Couldn't give process the ability to create a core file");
#endif
}

void
rootpriv(void)
{
    int status = seteuid(0);

    if (status) {
        log_syserr("Couldn't set effective user-ID to root's (0)");
    }
    else {
        ensureDumpable();
    }
}

void
unpriv(void)
{
    (void)seteuid(getuid()); // Can't fail
    ensureDumpable();
}

void
endpriv(void)
{
    (void)setuid(getuid()); // Can't fail
    ensureDumpable();       // Can't hurt
}

#ifdef TEST_PRIV

#include <stdio.h>

main()
{
        (void)printf("BEGIN euid %d, uid %d (sav %d)\n",
                         geteuid(), getuid(), sav_uid);
        rootpriv();
        (void)printf("R1    euid %d, uid %d (sav %d)\n",
                         geteuid(), getuid(), sav_uid);
        unpriv();
        (void)printf("U1    euid %d, uid %d (sav %d)\n",
                         geteuid(), getuid(), sav_uid);
        rootpriv();
        (void)printf("R2    euid %d, uid %d (sav %d)\n",
                         geteuid(), getuid(), sav_uid);
        unpriv();
        (void)printf("U2    euid %d, uid %d (sav %d)\n",
                         geteuid(), getuid(), sav_uid);
        exit(0);
}
#endif
