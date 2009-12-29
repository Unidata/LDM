/*
 * Copyright 2009 University Corporation for Atmospheric Research.
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This program is the registry utility.
 */
#include <config.h>

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "registry.h"
#include "ulog.h"
#include "log.h"

static const char*      _pathPrefix;
static const char*      _nodePath;
static StringBuf*       _valuePath;

static void printUsage(const char* progname)
{
    (void)fprintf(stderr,
        "Usage:\n"
        "  %s [path [value]]\n", progname);
}

/*
 * Prints a value.
 *
 * Arguments:
 *      vt              Pointer to the value to be printed.  Shall not be NULL.
 * Returns:
 *      0               Success
 */
static int printValue(
    ValueThing* const     vt)
{
    int         status = sb_set(_valuePath, _nodePath, REG_SEP, vt_getName(vt),
        NULL);

    if (0 != status) {
        log_add("Couldn't form pathname for value \"%s\"", vt_getName(vt));
    }
    else {
        (void)printf("%s : %s\n", sb_string(_valuePath), vt_getValue(vt));
    }

    return status;
}

/*
 * Prints the values of a node.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOENT          This node lies outside the scope of interest.
 */
static int printNodeValues(
    RegNode* const      node)
{
    int                 status;
    const char* const   absPath = reg_getNodeAbsPath(node);

    if (strstr(absPath, _pathPrefix) != absPath) {
        status = ENOENT;
    }
    else {
        if (0 == absPath[1]) {
            _nodePath = absPath+1;
        }
        else {
            _nodePath = absPath;
        }

        reg_visitValues(node, printValue);
    }

    return status;
}

/*
 * Prints to the standard output stream all values in the registry whose path
 * name starts with a given prefix.
 *
 * Arguments:
 *      prefix  The path name prefix.  Shall not be NULL.
 * Returns:
 *      0       Sucess
 *      else    Failure.  "log_start()" called.
 */
static int printValues(
    const char* const   path)
{
    RegNode*    node;
    int         status;

    if (0 == (status = reg_getNode(path, &node))) {
        _pathPrefix = path;
        status = reg_visitNodes(node, printNodeValues);

        if (0 == status || ENOENT == status)
            status = 0;                 /* success */
    }

    return status;
}

/*
 * Puts an entry into the registry.
 *
 * Arguments:
 *      key     The key of the entry.
 *      value   The value of the entry.
 * Returns:
 *      0       Failure.  "log_start()" called.
 *      else    Success.
 */
static int putEntry(
    const char* const   key,
    const char* const   value)
{
    return 0;   /* TODO */
}

int main(
    int         argc,
    char*       argv[])
{
    int status;

    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");

    if (status = sb_new(&_valuePath, 80)) {
        log_add("Couldn't initialize utility");
    }
    else {
        int                 ch;
        extern int          optind;
        extern int          opterr;
        extern char*        optarg;

        opterr = 0;                     /* supress getopt(3) error messages */

        while (0 == status && (ch = getopt(argc, argv, "r:")) != EOF) {
            switch (ch) {
            case 'r': {
                status = reg_setPathname(optarg);
                break;
            }
            default:
                log_start("Unknown option: \"%c\"\n", ch);
                log_log(LOG_ERR);
                printUsage(argv[0]);
                status = 1;
            }
        }                               /* options loop */

        if (0 == status) {
            int     argCount = argc - optind;

            if (0 == argCount) {
                if (status = printValues("/")) {
                    log_log(LOG_ERR);
                }
            }
            else if (1 == argCount) {
                if (status = printValues(argv[optind])) {
                    log_log(LOG_ERR);
                }
            }
            else if (2 == argCount) {
                if (status = putEntry(argv[optind], argv[optind+1])) {
                    log_log(LOG_ERR);
                }
            }
            else {
                (void)fprintf(stderr, "Too many arguments\n");
                printUsage(argv[0]);
                status = 1;
            }
        }

        sb_free(_valuePath);
    }                                   /* "_valuePath" allocated */

    return status;
}
