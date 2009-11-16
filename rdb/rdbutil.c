/*
 * Copyright 2009 University Corporation for Atmospheric Research.
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This program is the runtime database utility.
 */
#include <ldmconfig.h>

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>

#include "rdblib.h"
#include "ulog.h"
#include "log.h"


static void
printUsage(const char* progname)
{
    (void)fprintf(stderr,
        "Usage:\n"
        "  %s [key [value]]\n", progname);
}


/*
 * Prints the contents of the runtime database to the standard output stream.
 *
 * Returns:
 *      0       Failure.  "log_start()" called.
 *      else    Success.
 */
static int
printDatabase(void)
{
    return 0;   /* TODO */
}


/*
 * Prints the value to which a given key maps in the runtime database to the
 * standard output stream.
 *
 * Arguments:
 *      key             The key.
 * Returns:
 *      0       Failure.  "log_start()" called.
 *      else    Success.
 */
static int
printValue(const char* const key)
{
    Rdb*        rdb;
    /*
    RdbStatus   status = rdbOpen(rdb, path,
    const int           forWriting)
    */

    return 0;   /* TODO */
}


/*
 * Puts an entry into the runtime database.
 *
 * Arguments:
 *      key     The key of the entry.
 *      value   The value of the entry.
 * Returns:
 *      0       Failure.  "log_start()" called.
 *      else    Success.
 */
static int
putEntry(
    const char* const   key,
    const char* const   value)
{
    RdbStatus   status = rdbOpen(rdb, path, 0);
    return 0;   /* TODO */
}


int
main(
    int         argc,
    char*       argv[])
{
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    int                 ch;
    int                 exitStatus = EXIT_SUCCESS;

    opterr = 0;                         /* supress getopt(3) error messages */

    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");

    while ((ch = getopt(argc, argv, "")) != EOF) {
        switch (ch) {
        default:
            (void)fprintf(stderr, "Unknown option: \"%c\"\n", ch);
            printUsage(argv[0]);
            exitStatus = EXIT_FAILURE;
        }
    }                                   /* options loop */

    if (EXIT_SUCCESS == exitStatus) {
        int     argCount = argc - optind;

        if (0 == argCount) {
            if (!printDatabase()) {
                log_log(LOG_ERR);
                exitStatus = EXIT_FAILURE;
            }
        }
        else if (1 == argCount) {
            if (!printValue(argv[optind])) {
                log_log(LOG_ERR);
                exitStatus = EXIT_FAILURE;
            }
        }
        else if (2 == argCount) {
            if (!putEntry(argv[optind], argv[optind+1])) {
                log_log(LOG_ERR);
                exitStatus = EXIT_FAILURE;
            }
        }
        else {
            (void)fprintf(stderr, "Too many arguments\n");
            printUsage(argv[0]);
            exitStatus = EXIT_FAILURE;
        }
    }

    return exitStatus;
}
