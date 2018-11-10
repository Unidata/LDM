/* 
 * Grammar for LDM configuration-file.
 */

%{
/*
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

#include "config.h"

#include "ldm_config_file.h"
#include "atofeedt.h"
#include "error.h"
#include "globals.h"
#include "inetutil.h"
#include "remote.h"
#include "ldm.h"
#include "ldmprint.h"
#include "RegularExpressions.h"
#include "log.h"
#include "stdbool.h"
#include "wordexp.h"
#if WANT_MULTICAST
    #include "CidrAddr.h"
    #include "down7_manager.h"
    #include "mcast_info.h"
    #include "UpMcastMgr.h"
#endif

#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>

#if YYDEBUG
extern int yydebug;
#endif

static int       line = 0;
static unsigned  ldmPort = LDM_PORT;
static in_addr_t ldmIpAddr;
static int       scannerPush(const char* const path);
static int       scannerPop(void);

static void
yyerror(const char *msg)
{
    log_add("Error in LDM configuration-file: %s", msg);
}

#if __STDC__
extern int yyparse(void);
#endif


static int
decodeRegEx(
    regex_t** const     regexpp,
    const char*         string)
{
    int         error = 1;              /* failure */

    if (strlen(string) == 0) {
        log_error_q("Empty regular-expression");
    }
    else {
        char* const     clone = strdup(string);

        if (NULL == clone) {
            log_add("Couldn't clone regular-expression \"%s\": %s", string,
                    strerror(errno));
        }
        else {
            regex_t*    regexp = (regex_t*)malloc(sizeof(regex_t));

            if (NULL == regexp) {
                log_add("Couldn't allocate %lu bytes for \"regex_t\"",
                    (unsigned long)sizeof(regex_t));
            }
            else {
                if (re_vetSpec(clone)) {
                    /*
                     * Pathological regular expression.
                     */
                    log_warning_q("Adjusted pathological regular expression \"%s\"",
                        string);
                }

                error = regcomp(regexp, clone,
                        REG_EXTENDED|REG_ICASE|REG_NOSUB);

                if (!error) {
                    *regexpp = regexp;
                }
                else {
                    char        buf[132];

                    (void)regerror(error, regexp, buf, sizeof(buf));
                    log_add("Couldn't compile regular-expression \"%s\": %s",
                            clone, buf);
                }

                if (error)
                    free(regexp);
            }                           /* "regexp" allocated */

            free(clone);
        }                               /* "clone" allocated */
    }                                   /* non-empty regular-expression */

    return error;
}


static int
decodeHostSet(
    host_set** const    hspp,
    const char*         string)
{
    regex_t*    regexp;
    int         error = decodeRegEx(&regexp, string);

    if (!error) {
        char* dup = strdup(string);

        if (NULL == dup) {
            log_add("Couldn't clone string \"%s\": %s", string,
                strerror(errno));
        }
        else {
            host_set*   hsp = lcf_newHostSet(HS_REGEXP, dup, regexp);

            if (NULL == hsp) {
                log_add("Couldn't create host-set for \"%s\": %s", dup,
                        strerror(errno));

                error = 1;
            }
            else {
#if YYDEBUG
                if(yydebug)
                    udebug("hostset: \"%s\"", dup);
#endif
                *hspp = hsp;
                error = 0;
            }

            if (error)
                free(dup);
        }                               /* "dup" set */

        if (error)
            regfree(regexp);

        free(regexp);
    }                           /* "regexp" set */

    return error;
}


static int
decodeSelection(
    feedtypet* const    ftp,
    regex_t** const     regexpp,
    const char* const   ftString,
    const char* const   regexString)
{
    feedtypet   ft;
    int         error;

    error = decodeFeedtype(&ft, ftString);

    if (!error) {
        error = decodeRegEx(regexpp, regexString);

        if (!error) {
#if YYDEBUG
            if(yydebug)
                udebug("prodIdPat: \"%s\"", regexString);
#endif
            *ftp = ft;
        }
    }                           /* feedtype decoded */

    return error;
}


static void
warnIfPathological(
    const char*  const  re)
{
    if (re_isPathological(re)) {
        /*
         * Pathological regular expression.
         */
        log_warning_q("Pathological regular expression \"%s\"", re);
    }
}


/*
 * Arguments:
 *      feedtypeSpec    String specification of feedtype.  May not be NULL.
 *                      Caller may free upon return.
 *      hostPattern     ERE of allowed hosts.  May not be NULL.  Caller may
 *                      free upon return.
 *      okPattern       ERE that product-identifiers must match in order for
 *                      the associated data-products to be transferred.  Caller
 *                      may free upon return.
 *      notPattern      ERE that product-identifiers must NOT match in order for
 *                      the associated data-products to be transferred.  May
 *                      be null to indicate that such matching should be
 *                      disabled.  Caller may free upon return.
 * Returns:
 *      0               Success.
 *      else            Failure.  "log_add()" called.
 */
static int
decodeAllowEntry(
    const char* const   feedtypeSpec,
    const char* const   hostPattern,
    const char* const   okPattern,
    const char* const   notPattern)
{
    feedtypet   ft;
    int         errCode = decodeFeedtype(&ft, feedtypeSpec);

    if (!errCode) {
        host_set*       hsp;

        errCode = decodeHostSet(&hsp, hostPattern);

        if (!errCode) {
            ErrorObj*   errObj;

            warnIfPathological(okPattern);

            if (notPattern)
                warnIfPathological(notPattern);

            errObj = lcf_addAllow(ft, hsp, okPattern, notPattern);

            if (errObj) {
                log_add("Couldn't add ALLOW entry: feedSet=%s, hostPat=%s, "
                        "okPat=\"%s\", notPat=\"%s\"",
                        feedtypeSpec, hostPattern, okPattern, notPattern);
                lcf_freeHostSet(hsp);
                errCode = -1;
            }
        }                               /* "hsp" allocated */
    }                                   /* "ft" set */

    return errCode;
}


static int
decodeRequestEntry(
    const char* const   feedtypeSpec,
    const char* const   prodPattern,
    char* const         hostSpec)
{
    feedtypet   feedtype;
    regex_t*    regexp;
    int         errCode =
            decodeSelection(&feedtype, &regexp, feedtypeSpec, prodPattern);

    if (!errCode) {
        const char*    hostId = strtok(hostSpec, ":");
    
        if (NULL == hostId) {
            log_add("Invalid hostname specification \"%s\"", hostSpec);
    
            errCode = EINVAL;
        }
        else {
            unsigned    localPort;
            const char* portSpec = strtok(NULL, ":");
        
            if (NULL == portSpec) {
                localPort = ldmPort;
            }
            else {
                char*   suffix = "";
                long    port;
        
                errno = 0;
                port = strtol(portSpec, &suffix, 0);
        
                if (0 == errno && 0 == *suffix && 0 < port && 0xffff >= port) {
                    localPort = (unsigned)port;
                }
                else {
                    log_add("Invalid port specification \"%s\"", portSpec);
        
                    errCode = EINVAL;
                }
            } /* have port specification */
        
            if (0 == errCode) {
                if (errCode = lcf_addRequest(feedtype, prodPattern, hostId,
                        localPort)) {
                }
            } /* "localPort" set */
        } /* valid hostname */
    
        regfree(regexp);
        free(regexp);
    } /* "regexp" allocated */
    
    if (errCode)
        log_add("Couldn't process REQUEST: host=%s, feedSet=%s, "
                "prodPat=\"%s\"", hostSpec, feedtypeSpec, prodPattern);

    return errCode;
}

int
lcf_init(
    const char* const   pathname,
    in_addr_t           ldmAddr,
    unsigned            defaultPort) // Declared in ldm_config_file.h
{
    int status = 0;

#if WANT_MULTICAST
    /*
     * Initialize the upstream multicast manager.
     */
    status = umm_init();
    
    if (status) {
        log_add("Couldn't initialize upstream multicast manager");
        status = -1;
    }
#endif

    if (status == 0) {
        if (scannerPush(pathname)) {
            log_add("Couldn't open LDM configuration-file \"%s\"", pathname);
            status = -1;
        }
        else {
            ldmPort = defaultPort;
            ldmIpAddr = ldmAddr;
            // yydebug = 1;
            status = yyparse();

            if (status) {
                log_add("Couldn't parse LDM configuration-file \"%s\"", pathname);
                status = -1;
            }
        }

#if WANT_MULTICAST
        if (status)
            umm_destroy(true);
#endif
    } // Upstream multicast manager possibly initialized

    return status;
}

#if YYDEBUG
#define printf udebug
#endif

%}

%union  {
                char    string[2000];
        }


%token ACCEPT_K
%token ALLOW_K
%token EXEC_K
%token INCLUDE_K
%token RECEIVE_K
%token REQUEST_K
%token MULTICAST_K

%token <string> STRING

%start table

%%
table:          /* empty */
                | table entry
                ;

entry:            accept_entry
                | allow_entry
                | exec_entry
                | include_stmt
                | receive_entry
                | request_entry
                | multicast_entry
                ;

accept_entry:   ACCEPT_K STRING STRING STRING
                {
                    feedtypet   ft;
                    regex_t*    regexp;
                    int         error = decodeSelection(&ft, &regexp, $2, $3);

                    if (!error) {
                        host_set*       hsp;

                        error = decodeHostSet(&hsp, $4);

                        if (!error) {
                            char*       patp = strdup($3);

                            if (NULL == patp) {
                                log_add("Couldn't clone string \"%s\": %s",
                                    $3, strerror(errno));

                                error = 1;
                            }
                            else {
                                error =
                                    lcf_addAccept(ft, patp, regexp, hsp, 1);

                                if (!error) {
                                    patp = NULL;    /* abandon */
                                    hsp = NULL;     /* abandon */
                                    regexp = NULL;  /* abandon */
                                }
                                else {
                                    free(patp);
                                }
                            }           /* "patp" allocated */

                            if (error)
                                lcf_freeHostSet(hsp);
                        }               /* "*hsp" allocated */

                        if (error && regexp) {
                            regfree(regexp);
                            free(regexp);
                        }
                    }                   /* "regexp" allocated */

                    if (error) {
                        log_add("Couldn't process ACCEPT: feedSet=\"%s\""
                                "prodPat=\"%s\", hostPat=\"%s\"", $2, $3, $4);
                        return error;
                    }
                }
                ;

allow_entry:    ALLOW_K STRING STRING
                {
                    int errCode = decodeAllowEntry($2, $3, ".*", NULL);

                    if (errCode)
                        return errCode;
                }
                | ALLOW_K STRING STRING STRING
                {
                    int errCode = decodeAllowEntry($2, $3, $4, NULL);

                    if (errCode)
                        return errCode;
                }
                | ALLOW_K STRING STRING STRING STRING
                {
                    int errCode = decodeAllowEntry($2, $3, $4, $5);

                    if (errCode)
                        return errCode;
                }
                ;

exec_entry:     EXEC_K STRING
                {
                    wordexp_t   words;
                    int         error;

                    (void)memset(&words, 0, sizeof(words));

                    error = wordexp($2, &words, 0);

                    if (error) {
                        log_add("Couldn't decode command \"%s\": %s",
                            strerror(errno));
                    }
                    else {
#if YYDEBUG
                        if(yydebug)
                            udebug("command: \"%s\"", $2);
#endif
                        error = lcf_addExec(&words);
                        
                        if (error)
                            wordfree(&words);
                    }                   /* "words" set */

                    if (error) {
                        log_add("Couldn't process EXEC: cmd=\"%s\"", $2);
                        return error;
                    }
                }
                ;

include_stmt:   INCLUDE_K STRING
                {
                    if (scannerPush($2))
                        return -1;
                }

receive_entry:  RECEIVE_K STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeReceiveEntry($2, $3, NULL, NULL, NULL,
                            NULL);

                    if (errCode) {
                        log_add("Couldn't process receive entry "
                                "\"RECEIVE %s %s\"", $2, $3);
                        return errCode;
                    }
                #endif
                }
                | RECEIVE_K STRING STRING STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeReceiveEntry($2, $3, $4, $5, $6, NULL);

                    if (errCode) {
                        log_add("Couldn't process receive entry "
                                "\"RECEIVE %s %s %s %s %s\"", $2, $3, $4, $5,
                                $6);
                        return errCode;
                    }
                #endif
                }
                | RECEIVE_K STRING STRING STRING STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeReceiveEntry($2, $3, $4, $5, $6, $7);

                    if (errCode) {
                        log_add("Couldn't process receive entry "
                                "\"RECEIVE %s %s %s %s %s %s\"", $2, $3, $4, $5,
                                $6, $7);
                        return errCode;
                    }
                #endif
                }
                ;

request_entry:  REQUEST_K STRING STRING STRING
                {
                    int errCode = decodeRequestEntry($2, $3, $4);

                    if (errCode)
                        return errCode;
                }
                | REQUEST_K STRING STRING STRING STRING
                {
                    int errCode = decodeRequestEntry($2, $3, $4);

                    if (errCode)
                        return errCode;
                }
                ;

multicast_entry: MULTICAST_K STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeMulticastEntry($2, $3, $4, "0.0.0.0",
                            NULL, NULL, NULL, NULL, NULL);

                    if (errCode) {
                        log_add("Couldn't decode MULTICAST entry "
                                "\"MULTICAST %s %s %s\"", $2, $3, $4);
                        return errCode;
                    }
                #endif
                }
                | MULTICAST_K STRING STRING STRING STRING STRING STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeMulticastEntry($2, $3, $4, $5, $6, $7,
                            $8, $9, $5);

                    if (errCode) {
                        log_add("Couldn't decode MULTICAST entry "
                                "\"MULTICAST %s %s %s %s %s %s %s %s\"",
                                 $2, $3, $4, $5, $6, $7, $8, $9);
                        return errCode;
                    }
                #endif
                }
                | MULTICAST_K STRING STRING STRING STRING STRING STRING STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeMulticastEntry($2, $3, $4, $5, $6, $7,
                            $8, $9, $10);

                    if (errCode) {
                        log_add("Couldn't decode MULTICAST entry "
                                "\"MULTICAST %s %s %s %s %s %s %s %s %s\"",
                                 $2, $3, $4, $5, $6, $7, $8, $9, $10);
                        return errCode;
                    }
                #endif
                }
                ;

%%

#include "scanner.c"

/*
 * Returns:
 *       0      More input
 *      !0      No more input
 */
int
yywrap(void)
{
    return scannerPop();
}
