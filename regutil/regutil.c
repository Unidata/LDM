/**
 * Accesses the LDM registry.
 *
 * Copyright 2019, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */
#include <config.h>

#undef NDEBUG

#include "globals.h"
#include "ldmprint.h"
#include "log.h"
#include "registry.h"
#include "string_buf.h"

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef enum {
    COMMAND_SYNTAX = 1,
    NO_SUCH_ENTRY,
    SYSTEM_ERROR
}       Status;

static const char*      _pathPrefix;
static const char*      _nodePath;
static StringBuf*       _valuePath;

static void usage(const char* progname)
{
    log_add(
"Usages:\n"
"  Create Registry:     %s [-v|-x] [-d dir] -c\n"
"  Reset Registry:      %s [-v|-x] [-d dir] -R\n"
"  Print Parameters:    %s [-v|-x] [-d dir] [-q] [path ...]\n"
"  Remove Parameter(s): %s [-v|-x] [-d dir] [-q] -r path ...\n"
"  Set Parameter:       %s [-v|-x] [-d dir] (-b bool|-h sig|-s string|-t time|-u uint) "
    "valpath\n"
"Where:\n"
"  -b bool      Boolean registry value: TRUE, FALSE\n"
"  -d dir       Path name of registry directory. Default=\"%s\"\n"
"  -h sig       Data-product signature as 32 hexadecimal characters\n"
"  -q           Be quiet about missing values or nodes\n"
"  -s string    String registry value\n"
"  -t time      Time registry value as YYYYMMDDThhmmss[.uuuuuu]\n"
"  -u uint      Unsigned integer registry value\n"
"  -v           Log INFO messages\n"
"  -x           Log DEBUG messages\n"
"  path         Absolute pathname of registry node or value\n"
"  valpath      Absolute pathname of value\n",
        progname, progname, progname, progname, progname, getRegistryDirPath());
    log_flush_error();
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
#if 0
static int printValue(
    const char* const   path,
    const char* const   value)
{
    (void)printf("%s : %s\n", path, value);

    return 0;
}
#endif

/*
 * Prints a value-thing.  This function is designed to be called by the
 * registry module.
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
 * Prints the values of a node.  This function is designed to be called by the
 * registry module.
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
 *      path            Pointer to a registry pathname to be printed.  Shall
 *                      not be NULL.
 *      quiet           Whether or not to be quiet about a pathname not
 *                      existing.
 * Returns:
 *      0               Success
 *      NO_SUCH_ENTRY   No such value or node.  "log_flush()" called iff "quiet
 *                      == 0".
 *      SYSTEM_ERROR    Failure.  "log_flush()" called.
 */
static Status printPath(
    const char*         path,
    const int           quiet)
{
    Status      status = 0;             /* success */
    RegStatus   regStatus;
    char*       value;

    log_debug("%s printing path \"%s\"", quiet ? "Quietly" : "Non-quietly", path);

    /*
     * The path name is first assumed to reference an existing value;
     * otherwise, the value wouldn't be printed.
     */ 
    if (0 == (regStatus = reg_getString(path, &value))) {
        if (NULL != value) {
            (void)printf("%s\n", value);
            free(value);
        }                               /* "value" allocated */
    }                                   /* got value-string */
    else {
        if (ENOENT != regStatus) {
            log_flush_error();
            status = SYSTEM_ERROR;
        }
        else {
            /*
             * The path must reference a node.
             */
            RegNode*    node;

            log_clear();

            if (0 != (regStatus = reg_getNode(path, &node, 0))) {
                if (ENOENT == regStatus) {
                    if (!quiet) {
                        log_error_q("No such value or node: \"%s\"", path);
                    }
                    status = NO_SUCH_ENTRY;
                }
                else {
                    log_flush_error();
                    status = SYSTEM_ERROR;
                }
            }                           /* didn't get node */
            else {
                _pathPrefix = path;

                if (0 != reg_visitNodes(node, printNodeValues)) {
                    log_flush_error();
                    status = SYSTEM_ERROR;
                }                       /* error visiting nodes */
            }                           /* got node */
        }                               /* no such value */
    }                                   /* didn't get value-string */

    return status;
}

/*
 * Creates the registry.
 *
 * Returns:
 *      0               Success.
 *      SYSTEM_ERROR    System error.  "log_flush()" called.
 */
static Status createRegistry(void)
{
    RegNode*    rootNode;

    log_debug("Creating registry");

    if (0 != reg_getNode("/", &rootNode, 1)) {
        log_error_q("Couldn't create registry");
        return SYSTEM_ERROR;
    }

    return 0;
}

/*
 * Resets an existing registry.
 *
 * Returns:
 *      0               Success.
 *      SYSTEM_ERROR    System error.  "log_flush()" called.
 */
static Status resetRegistry(void)
{
    log_debug("Resetting registry");

    if (0 != reg_reset()) {
        log_error_q("Couldn't reset registry");
        return SYSTEM_ERROR;
    }

    return 0;
}

/*
 * Removes the values referenced by an absolute registry pathname.
 *
 * Arguments:
 *      path            Pointer to a an absolute registry pathname.  Shall not
 *                      be NULL.  If the pathname refers to a node, then the
 *                      node and all its subnodes are recursively removed.
 *      quiet           Whether or not to be quiet about a pathname not
 *                      existing.
 * Returns:
 *      0               Success.
 *      NO_SUCH_ENTRY   No such entry in the registry.  "log_flush()" called iff
 *                      "quiet == 0".
 *      SYSTEM_ERROR    System error.  "log_flush()" called.
 */
static Status deletePath(
    const char* const   path,
    const int           quiet)
{
    log_debug("%s deleting path \"%s\"", quiet ? "Quietly" : "Non-quietly", path);

    switch (reg_deleteValue(path)) {
        case 0:
            return 0;

        case ENOENT: {
            RegNode*        node;

            switch (reg_getNode(path, &node, 0)) {
                case 0:
                    reg_deleteNode(node);

                    if (reg_flushNode(node)) {
                        log_flush_error();
                        return SYSTEM_ERROR;
                    }

                    return 0;
                case ENOENT:
                    if (!quiet) {
                        log_error_q("No such value or node: \"%s\"", path);
                    }
                    return NO_SUCH_ENTRY;
                default:
                    log_flush_error();
                    return SYSTEM_ERROR;
            }
        }
        /* FALLTHROUGH */
        /* no break */

        default:
            log_flush_error();
            return SYSTEM_ERROR;
    }
}

/*
 * Acts upon a list of registry pathnames.
 *
 * Arguments:
 *      pathList        Pointer to a NULL-terminated array of pointers to
 *                      absolute registry pathnames.  Shall not be NULL.
 *      func            Pointer to the function to be applied to each pathname.
 *                      The function shall return one of
 *                          0   Success.
 *                          NO_SUCH_ENTRY       No such entry.  "log_add()"
 *                                              called iff "quiet == 0".
 *                          SYSTEM_ERROR        System error.  "log_add()"
 *                                              called.
 *      quiet           Whether or not to be quiet about a pathname not
 *                      existing.
 * Returns:
 *      0               Success.
 *      NO_SUCH_ENTRY   An entry didn't exist.  "log_flush()" called.  All
 *                      pathnames were acted upon.
 *      SYSTEM_ERROR    System error.  "log_flush()" called.  Processing
 *                      terminated with the pathname that caused the error.
 */
static Status actUponPathList(
    char* const*        pathList,
    Status              (*func)(const char* path, int quiet),
    int                 quiet)
{
    Status              status = 0;
    const char*         path;

    while (SYSTEM_ERROR > status && NULL != (path = *pathList++)) {
        Status  stat = func(path, quiet);

        if (stat) {
            /*
             * "status" should indicate the worse thing that happened.
             */
            if (stat > status)
                status = stat;
        }
    }

    return status;
}

/*
 * Returns:
 *      0       Success
 *      1       Incorrect usage (i.e., command-line syntax error).
 *      2       No such parameter or node.  Error message written.
 *      3       System error.  Error message written.
 */
int main(
    int         argc,
    char*       argv[])
{
    int                 status;
    const char* const   progname = basename(argv[0]);

    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        status = EXIT_FAILURE;
    }
    else {
        if ((status = sb_new(&_valuePath, 80))) {
            log_error_q("Couldn't initialize utility");
            status = SYSTEM_ERROR;
        }
        else {
            enum {
                UNKNOWN,
                CREATE,
                PRINT,
                PUT_BOOL,
                PUT_STRING,
                PUT_UINT,
                PUT_SIGNATURE,
                PUT_TIME,
                RESET,
                REMOVE
            }               action = UNKNOWN;
            const char*     string;
            signaturet      signature;
            timestampt      timestamp;
            unsigned long   uint;
            int             boolean;
            int             ch;
            int             quiet = 0;

            opterr = 0;                     /* supress getopt(3) error messages */

            while (0 == status && (ch = getopt(argc, argv, ":b:cd:h:l:qRrs:t:u:vx"))
                    != -1) {
                switch (ch) {
                case 'b': {
                    if (strcasecmp(optarg, "TRUE") == 0) {
                        boolean = 1;
                    }
                    else if (strcasecmp(optarg, "FALSE") == 0) {
                        boolean = 0;
                    }
                    else {
                        log_add("Not a boolean value: \"%s\"", optarg);
                        status = COMMAND_SYNTAX;
                    }

                    if (status == 0) {
                        if (CREATE == action) {
                            log_error_q("Create option ignored");
                        }
                        action = PUT_BOOL;
                    }
                    break;
                }
                case 'c': {
                    if (UNKNOWN != action) {
                        log_add("Can't mix create action with other actions");
                        status = COMMAND_SYNTAX;
                    }
                    else {
                        action = CREATE;
                    }
                    break;
                }
                case 'd': {
                    if ((status = reg_setDirectory(optarg)))
                        status = SYSTEM_ERROR;
                    break;
                }
                case 'h': {
                    status = sigParse(optarg, &signature);

                    if (0 > status || 0 != optarg[status]) {
                        log_add("Not a signature: \"%s\"", optarg);
                        status = COMMAND_SYNTAX;
                    }
                    else {
                        if (CREATE == action) {
                            log_info_q("Create action ignored");
                        }
                        action = PUT_SIGNATURE;
                        status = 0;
                    }
                    break;
                }
                case 'l': {
                    log_set_destination(optarg);
                    break;
                }
                case 'q': {
                    quiet = 1;
                    break;
                }
                case 'R': {
                    if (UNKNOWN != action) {
                        log_add("Can't mix reset action with other actions");
                        status = COMMAND_SYNTAX;
                    }
                    else {
                        action = RESET;
                    }
                    break;
                }
                case 'r': {
                    if (UNKNOWN != action) {
                        log_add("Can't mix remove action with other actions");
                        status = COMMAND_SYNTAX;
                    }
                    else {
                        action = REMOVE;
                    }
                    break;
                }
                case 's': {
                    if (CREATE == action) {
                        log_info_q("Create action  ignored");
                    }
                    string = optarg;
                    action = PUT_STRING;
                    break;
                }
                case 't': {
                    status = tsParse(optarg, &timestamp);

                    if (0 > status || 0 != optarg[status]) {
                        log_add("Not a timestamp: \"%s\"", optarg);
                        status = COMMAND_SYNTAX;
                    }
                    else {
                        if (CREATE == action) {
                            log_info_q("Create action ignored");
                        }
                        action = PUT_TIME;
                        status = 0;
                    }
                    break;
                }
                case 'u': {
                    char*   end;

                    errno = 0;
                    uint = strtoul(optarg, &end, 0);

                    if (0 != *end || (0 == uint && 0 != errno)) {
                        log_add("Not an unsigned integer: \"%s\"", optarg);
                        status = COMMAND_SYNTAX;
                    }
                    else {
                        if (CREATE == action) {
                            log_info_q("Create option ignored");
                        }
                        action = PUT_UINT;
                    }
                    break;
                }
                case 'v': {
                    if (!log_is_enabled_info)
                        (void)log_set_level(LOG_LEVEL_INFO);
                    break;
                }
                case 'x': {
                    (void)log_set_level(LOG_LEVEL_DEBUG);
                    break;
                }
                case ':': {
                    log_add("Option \"-%c\" requires an operand", optopt);
                    status = COMMAND_SYNTAX;
                    break;
                }
                default:
                    log_add("Unknown option: \"%c\"", optopt);
                    status = COMMAND_SYNTAX;
                    /* no break */
                }
            }                               /* options loop */

            if (status) {
                log_flush_error();

                if (COMMAND_SYNTAX == status)
                    usage(progname);
            }
            else {
                const int     argCount = argc - optind;

                if (UNKNOWN == action)
                    action = PRINT;

                switch (action) {
                    case CREATE: {
                        if (0 < argCount) {
                            log_error_q("Too many arguments");
                            usage(progname);
                            status = COMMAND_SYNTAX;
                        }
                        else {
                            status = createRegistry();
                        }
                        break;
                    }
                    case RESET: {
                        if (0 < argCount) {
                            log_error_q("Too many arguments");
                            usage(progname);
                            status = COMMAND_SYNTAX;
                        }
                        else {
                            status = resetRegistry();
                        }
                        break;
                    }
                    case REMOVE: {
                        if (0 == argCount) {
                            log_error_q(
                                "Removal action requires absolute pathname(s)");
                            usage(progname);
                            status = COMMAND_SYNTAX;
                        }
                        else {
                            log_debug("Removing registry");
                            status = actUponPathList(argv + optind, deletePath,
                                quiet);
                        }
                        break;
                    }
                    case PRINT: {
                        log_debug("Printing registry");
                        status = (0 == argCount)
                            ? printPath("/", quiet)
                            : actUponPathList(argv + optind, printPath, quiet);
                        break;
                    }
                    default: {
                        /*
                         * Must be some kind of "put".
                         */
                        if (0 == argCount) {
                            log_error_q("Put action requires value pathname");
                            usage(progname);
                            status = COMMAND_SYNTAX;
                        }
                        else {
                            switch (action) {
                            case PUT_BOOL:
                                status = reg_putBool(argv[optind], boolean);
                                break;
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
                                log_flush_error();
                                status = SYSTEM_ERROR;
                            }
                        }
                    }                       /* put switch */
                    /* no break */
                }                           /* "action" switch */
            }                               /* decoded options */

            sb_free(_valuePath);
        }                                   /* "_valuePath" allocated */
    }

    return status;
}
