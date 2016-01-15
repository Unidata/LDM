/*
 *   Copyright 2015, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */

/*
 * Persists state (e.g., time of last-processed data-product) of pqact(1)
 * processes between invocations.
 */

#include <config.h>
#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "timestamp.h"
#include "mylog.h"

#include "state.h"

static char*    statePathname = NULL;
static char*    tmpStatePathname = NULL;


/*
 * Initializes this module.
 *
 * ARGUMENTS:
 *      configPathname  The pathname of the pqact(1) configuration-file.
 * RETURNS:
 *      0       Success
 *      -1      Invalid argument
 *      -2      System error.  See "errno".
 */
int
stateInit(
    const char* const   configPathname)
{
    int         status;

    if (configPathname == NULL) {
        mylog_add("stateInit(): Pathname is NULL");
        status = -1;
    }
    else {
        static const char* const        extension = ".state";
        size_t                          pathlen = 
            strlen(configPathname) + strlen(extension) + 1;
        char*                           newPath = malloc(pathlen);

        if (newPath == NULL) {
            mylog_add_syserr("Couldn't allocate %lu-byte pathname",
                (unsigned long)pathlen);
            status = -2;
        }
        else {
            static const char* const    tmpExt = ".tmp";
            char*                       newTmpPath;

            pathlen += strlen(tmpExt);
            newTmpPath = malloc(pathlen);

            if (newTmpPath == NULL) {
                mylog_add_syserr("Couldn't allocate %lu-byte pathname",
                    (unsigned long)pathlen);
                free(newPath);
                status = -2;
            }
            else {
                (void)strcpy(newPath, configPathname);
                (void)strcat(newPath, extension);
                free(statePathname);
                statePathname = newPath;

                (void)strcpy(newTmpPath, newPath);
                (void)strcat(newTmpPath, tmpExt);
                free(tmpStatePathname);
                tmpStatePathname = newTmpPath;

                status = 0;
            } // `newTmpPath` allocated
        } // `newpath` allocated
    }                                   /* configPathname != NULL */

    return status;
}


/**
 * Reads information from the state file.
 *
 * @param[in] pqCursor  The product-queue cursor to have its time values set
 *                      from the state file.
 * @retval    0         Success
 * @retval    -1        Module not initialized.
 * @retval    -2        Couldn't open state file for reading.
 * @retval    -3        Couldn't read information from state file.
 */
int
stateRead(
    timestampt* const   pqCursor)
{
    int         status;

    if (statePathname == NULL) {
        mylog_add("stateRead(): stateInit() not successfully called");
        status = -1;                    /* module not initialized */
    }
    else {
        FILE*   file = fopen(statePathname, "r");

        if (file == NULL) {
            mylog_add_syserr("stateRead(): Couldn't open \"%s\"", statePathname);
            status = -2;                /* couldn't open state-file */
        }
        else {
            int         c;

            status = -3;                /* state-file is corrupt */

            while ((c = fgetc(file)) == '#')
                (void)fscanf(file, "%*[^\n]\n");

            if (ferror(file)) {
                mylog_syserr("Couldn't read comments from \"%s\"",
                        statePathname);
            }
            else {
                unsigned long   seconds;
                long            microseconds;

                ungetc(c, file);

                if (fscanf(file, "%lu.%ld", &seconds, &microseconds) != 2) {
                    mylog_add("Couldn't read time from \"%s\"", statePathname);
                }
                else {
                    pqCursor->tv_sec = seconds;
                    pqCursor->tv_usec = microseconds;
                    status = 0;         /* success */
                }                       /* read time */
            }                           /* read comments */

            (void)fclose(file);
        }                               /* file open */
    }                                   /* module initialized */

    return status;
}


/*
 * Writes information to the state file.
 *
 * ARGUMENTS:
 *      pqCursor        The product-queue cursor to have its time values written
 *                      to the state file.
 * RETURNS:
 *      0       Success
 *      -1      Module not initialized.
 *      -2      Couldn't open state file for writing.
 *      -3      Couldn't write information to state file.
 */
int
stateWrite(
    const timestampt* const     pqCursor)
{
    int         status;

    if (statePathname == NULL) {
        mylog_add("stateWrite(): stateInit() not successfully called");
        status = -1;
    }
    else {
        FILE*   file = fopen(tmpStatePathname, "w");

        if (file == NULL) {
            mylog_syserr("Couldn't open \"%s\"", tmpStatePathname);
            status = -2;
        }
        else {
            status = -3;

            if (fputs(
"# The following line contains the insertion-time of the last, successfully-\n"
"# processed data-product.  Do not modify it unless you know exactly what\n"
"# you're doing!\n", file) < 0) {
                mylog_syserr("Couldn't write comment to \"%s\"",
                    tmpStatePathname);
            }
            else {
                if (fprintf(file, "%lu.%06lu\n",
                        (unsigned long)pqCursor->tv_sec, 
                        (unsigned long)pqCursor->tv_usec) < 0) {
                    mylog_syserr("Couldn't write time to \"%s\"",
                        tmpStatePathname);
                }
                else {
                    if (rename(tmpStatePathname, statePathname) == -1) {
                        mylog_syserr("Couldn't rename \"%s\" to \"%s\"",
                            tmpStatePathname, statePathname);
                    }
                    else {
                        status = 0;
                    }
                }
            }

            (void)fclose(file);
        }                               /* output file open */
    }                                   /* module initialized */

    return status;
}
