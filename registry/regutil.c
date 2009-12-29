/*
 * Copyright 2009 University Corporation for Atmospheric Research.
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This program is the registry utility.
 */
#include <config.h>

#undef NDEBUG
#include <assert.h>
#include <errno.h>
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
        "Usages:\n"
        "  %s [-r dir] [path]\n"
        "  %s [-r dir] (-c sig|-s string|-t time|-u uint) valpath\n"
        "where:\n"
        "  dir          Path name of registry directory\n"
        "  path         Absolute path name of registry node or value\n"
        "  valpath      Absolute path name of value\n"
        "  sig          Data-product signature as 32 hexadecimal characters\n"
        "  string       String registry value\n"
        "  time         Time registry value as YYYYMMDDThhmmss.mmmmmm\n"
        "  uint         Unsigned integer registry value\n",
        progname, progname);
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

int main(
    int         argc,
    char*       argv[])
{
    int                 status;
    const char* const   progname = basename(argv[0]);

    (void) openulog(progname, LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");

    if (status = sb_new(&_valuePath, 80)) {
        log_add("Couldn't initialize utility");
    }
    else {
        enum {
            PRINT,
            PUT_STRING,
            PUT_UINT,
            PUT_SIGNATURE,
            PUT_TIME,
        }                   usage = PRINT;
        int                 ch;
        extern int          optind;
        extern int          opterr;
        extern char*        optarg;
        const char*         string;
        signaturet          signature;
        timestampt          timestamp;
        unsigned long       uint;

        opterr = 0;                     /* supress getopt(3) error messages */

        while (0 == status && (ch = getopt(argc, argv, "c:r:s:t:u:")) != EOF) {
            switch (ch) {
            case 'c': {
                status = sigParse(optarg, &signature);

                if (0 > status || 0 != optarg[status]) {
                    log_start("Not a signature: \"%s\"", optarg);
                    status = EILSEQ;
                }
                else {
                    usage = PUT_SIGNATURE;
                }
                break;
            }
            case 'r': {
                status = reg_setPathname(optarg);
                break;
            }
            case 's': {
                string = optarg;
                usage = PUT_STRING;
                break;
            }
            case 't': {
                status = tsParse(optarg, &timestamp);

                if (0 > status || 0 != optarg[status]) {
                    log_start("Not a timestamp: \"%s\"", optarg);
                    status = EILSEQ;
                }
                else {
                    usage = PUT_TIME;
                }
                break;
            }
            case 'u': {
                char*   end;

                errno = 0;
                uint = strtoul(optarg, &end, 0);

                if (0 != *end || (0 == uint && 0 != errno)) {
                    log_start("Not an unsigned integer: \"%s\"", optarg);
                    status = EILSEQ;
                }
                else {
                    usage = PUT_UINT;
                }
                break;
            }
            default:
                log_start("Unknown option: \"%c\"\n", ch);
                status = 1;
            }

            if (status) {
                log_log(LOG_ERR);
                printUsage(progname);
            }
        }                               /* options loop */

        if (0 == status) {
            int     argCount = argc - optind;

            if (PRINT == usage) {
                if (1 < argCount) {
                    log_start("Too many arguments");
                    log_log(LOG_ERR);
                    printUsage(progname);
                    status = 1;
                }
                else if (status =
                        printValues(0 == argCount ? "/" : argv[optind])) {
                    log_log(LOG_ERR);
                }
            }
            else if (1 != argCount) {
                log_start("Path name of value not specified");
                log_log(LOG_ERR);
                printUsage(progname);
                status = 1;
            }
            else {
                switch (usage) {
                case PUT_UINT:
                    status = reg_putUint(argv[optind], uint);
                    break;
                case PUT_STRING:
                    status = reg_putString(argv[optind], string);
                    break;
                case PUT_TIME:
                    status = reg_putTime(argv[optind], &timestamp);
                    break;
                case PUT_SIGNATURE:
                    status = reg_putSignature(argv[optind], signature);
                    break;
                default:
                    assert(0);
                }
                if (status)
                    log_log(LOG_ERR);
            }
        }

        sb_free(_valuePath);
    }                                   /* "_valuePath" allocated */

    return status;
}
