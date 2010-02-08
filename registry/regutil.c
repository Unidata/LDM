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
#include <unistd.h>

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
        "  Create Registry:  %s [-d dir] -c\n"
        "  Reset Registry:   %s [-d dir] -r\n"
        "  Print Parameters: %s [-d dir] [path]\n"
        "  Set Parameter:    %s [-d dir] (-h sig|-s string|-t time|-u uint) "
            "valpath\n"
        "where:\n"
        "  dir          Path name of registry directory\n"
        "  path         Absolute path name of registry node or value\n"
        "  valpath      Absolute path name of value\n"
        "  sig          Data-product signature as 32 hexadecimal characters\n"
        "  string       String registry value\n"
        "  time         Time registry value as YYYYMMDDThhmmss.uuuuuu\n"
        "  uint         Unsigned integer registry value\n",
        progname, progname, progname, progname);
}

/*
 * Prints a value.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.
 *      value           Pointer to the value to be printed.  Shall not be NULL.
 * Returns:
 *      0               Success
 */
static int printValue(
    const char* const   path,
    const char* const   value)
{
    (void)printf("%s : %s\n", path, value);

    return 0;
}

/*
 * Prints a value-thing.
 *
 * Arguments:
 *      vt              Pointer to the value-thing to be printed.  Shall not be
 *                      NULL.
 * Returns:
 *      0               Success
 */
static int printValueThing(
    ValueThing* const     vt)
{
    int         status = sb_set(_valuePath, _nodePath, REG_SEP, vt_getName(vt),
        NULL);

    if (0 != status) {
        log_add("Couldn't form pathname for value \"%s\"", vt_getName(vt));
    }
    else {
        (void)printf("%s : %s\n", sb_string(_valuePath), vt_getValue(vt));
        status = 0;
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

        status = reg_visitValues(node, printValueThing);
    }

    return status;
}

/*
 * Prints to the standard output stream all values in the registry whose path
 * name starts with a given prefix.
 *
 * Arguments:
 *      path    The path name prefix.  Shall not be NULL.
 * Returns:
 *      0       Sucess
 *      ENOENT  No such value or node.  "log_start()" called.
 *      else    Failure.  "log_start()" called.
 */
static int printValues(
    const char* const   path)
{
    char*       value;
    /*
     * The path name is first assumed to reference an existing value;
     * otherwise, the value wouldn't be printed.
     */ 
    int         status = reg_getString(path, &value);

    if (0 == status && NULL != value) {
        (void)printf("%s\n", value);
        free(value);
    }                                   /* "value" allocated */
    else if (ENOENT == status) {
        RegNode*    node;

        log_clear();

        if (0 != (status = reg_getNode(path, &node, 0))) {
            if (ENOENT == status)
                log_start("No such value or node: \"%s\"", path);
        }
        else {
            _pathPrefix = path;
            status = reg_visitNodes(node, printNodeValues);

            if (ENOENT == status)
                status = 0;                 /* success */
        }
    }

    return status;
}

/*
 * Returns:
 *      0       Success
 *      1       No such parameter or node.  Error message written.
 *      2       Something else.  Error message written.
 */
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
            UNKNOWN,
            CREATE,
            PRINT,
            PUT_STRING,
            PUT_UINT,
            PUT_SIGNATURE,
            PUT_TIME,
            RESET,
        }               usage = UNKNOWN;
        const char*     string;
        signaturet      signature;
        timestampt      timestamp;
        unsigned long   uint;
        int             ch;

        opterr = 0;                     /* supress getopt(3) error messages */

        while (0 == status && (ch = getopt(argc, argv, ":cd:h:rs:t:u:"))
                != -1) {
            switch (ch) {
            case 'c': {
                if (UNKNOWN != usage) {
                    log_start("Can't mix create action with other actions");
                    status = 1;
                }
                else {
                    usage = CREATE;
                }
                break;
            }
            case 'd': {
                status = reg_setPathname(optarg);
                break;
            }
            case 'h': {
                status = sigParse(optarg, &signature);

                if (0 > status || 0 != optarg[status]) {
                    log_start("Not a signature: \"%s\"", optarg);
                    status = EILSEQ;
                }
                else {
                    if (CREATE == usage) {
                        log_start("Create action ignored");
                        log_log(LOG_INFO);
                    }
                    usage = PUT_SIGNATURE;
                }
                break;
            }
            case 'r': {
                if (UNKNOWN != usage) {
                    log_start("Can't mix reset action with other actions");
                    status = 1;
                }
                else {
                    usage = RESET;
                }
                break;
            }
            case 's': {
                if (CREATE == usage) {
                    log_start("Create action  ignored");
                    log_log(LOG_INFO);
                }
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
                    if (CREATE == usage) {
                        log_start("Create action ignored");
                        log_log(LOG_INFO);
                    }
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
                    if (CREATE == usage) {
                        log_start("\"-c\" option ignored");
                        log_log(LOG_INFO);
                    }
                    usage = PUT_UINT;
                }
                break;
            }
            case ':': {
                log_start("Option \"-%c\" requires an operand", optopt);
                status = 1;
            }
            case '?':
                log_start("Unknown option: \"%c\"", optopt);
                status = 1;
            }
        }                               /* options loop */

        if (status) {
            log_log(LOG_ERR);
            printUsage(progname);
        }
        else {
            const int     argCount = argc - optind;

            if (UNKNOWN == usage)
                usage = PRINT;

            if (CREATE == usage) {
                if (0 < argCount) {
                    log_start("Too many arguments");
                    log_log(LOG_ERR);
                    printUsage(progname);
                    status = 2;
                }
                else {
                    RegNode*        rootNode;

                    if (0 != (status = reg_getNode("/", &rootNode, 1))) {
                        log_add("Couldn't create registry");
                        log_log(LOG_ERR);
                        status = 2;
                    }
                }
            }
            else if (RESET == usage) {
                if (0 < argCount) {
                    log_start("Too many arguments");
                    log_log(LOG_ERR);
                    printUsage(progname);
                    status = 2;
                }
                else {
                    if (0 != reg_reset()) {
                        log_add("Couldn't reset registry");
                        log_log(LOG_ERR);
                        status = 2;
                    }
                }
            }
            else if (PRINT == usage) {
                if (1 < argCount) {
                    log_start("Too many arguments");
                    log_log(LOG_ERR);
                    printUsage(progname);
                    status = 2;
                }
                else if (status =
                        printValues(0 == argCount ? "/" : argv[optind])) {
                    log_log(LOG_ERR);
                    status = ENOENT == status ? 1 : 2;
                }
            }
            else if (1 != argCount) {
                log_start("Path name of node or value not specified");
                log_log(LOG_ERR);
                printUsage(progname);
                status = 2;
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
                    abort();
                }
                if (status) {
                    log_log(LOG_ERR);
                    status = 2;
                }
            }
        }

        sb_free(_valuePath);
    }                                   /* "_valuePath" allocated */

    return status;
}
