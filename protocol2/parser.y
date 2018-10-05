/* 
 * Grammar for LDM configuration-file.
 */

%{
/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
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
static int       execute = 1;
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
decodeFeedtype(
    feedtypet*  ftp,
    const char* string)
{
    feedtypet   ft;
    int         error;
    int         status = strfeedtypet(string, &ft);

    if (status == FEEDTYPE_OK) {
#if YYDEBUG
        if(yydebug)
            udebug("feedtype: %#x", ft);
#endif
        *ftp = ft;
        error = 0;
    }
    else {
        log_add("Invalid feedtype expression \"%s\": %s", string,
            strfeederr(status));

        error = 1;
    }

    return error;
}


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


#if WANT_MULTICAST
static bool
decodeSubnet(
        const char* const restrict addrsSpec,
        struct in_addr*            netPrefix,
        unsigned*                  prefixLen)
{
    struct in_addr addr;
    addr.s_addr = inet_addr(addrsSpec);
    if (addr.s_addr == (in_addr_t)(-1)) {
        log_add("Couldn't decode network-prefix");
        return false;
    }
    *netPrefix = addr;
    const char* cp = strchr(addrsSpec, '/');
    if (cp == NULL) {
        log_add("Couldn't find network-prefix length");
        return false;
    }
    if (scanf(cp+1, "%u", prefixLen) != 1) {
        log_add("Couldn't decode network-prefix length");
        return false;
    }
    if (*prefixLen > 32) {
        log_add("Invalid network-prefix length");
        return false;
    }
    return true;
}

/**
 * Decodes a MULTICAST entry.
 * @param[in] feedSpec        Specification of feedtype.
 * @param[in] mcastGroupSpec  Specification of multicast group.
 * @param[in] ttlSpec         Specification of time-to-live for multicast
 *                            packets.
 * @param[in] fmtpAddrSpec    Specification of IPv4 address of local FMTP server
 * @param[in] vlanIdSpec      Specification of VLAN identifier
 * @param[in] switchSpec      Specification of layer-2 switch
 * @param[in] switchPortSpec  Specification of port on OSI layer-2 switch
 * @param[in] fmtpSubnetSpec  Specification of IPv4 address-space for clients
 * @retval    0               Success.
 * @retval    EINVAL          Invalid specification. `log_add()` called.
 * @retval    ENOMEM          Out-of-memory. `log_add()` called.
 */
static int
decodeMulticastEntry(
    const char* const   feedSpec,
    const char* const   mcastGroupSpec,
    const char* const   ttlSpec,
    const char* const   fmtpAddrSpec,
    const char* const   vlanIdSpec,
    const char* const   switchSpec,
    const char* const   switchPortSpec,
    const char* const   fmtpSubnetSpec,
    const char* const   mcastIfaceSpec)
{
    int         status = EINVAL;
    feedtypet   feed;

    status = decodeFeedtype(&feed, feedSpec);
    
    if (0 == status) {
        ServiceAddr* mcastGroupSa = NULL;
        
        if ((status = sa_parseWithDefaults(&mcastGroupSa, mcastGroupSpec, NULL,
                38800))) {
            log_add("Couldn't parse multicast group specification: \"%s\"", 
                    mcastGroupSpec);
        }
        else {
            unsigned short ttl;
            int            nbytes;

            if (sscanf(ttlSpec, "%hu %n", &ttl, &nbytes) != 1 ||
                    ttlSpec[nbytes] != 0) {
                log_add("Couldn't parse time-to-live specification: "
                        "\"%s\"",  ttlSpec);
            }
            else {
                ServiceAddr* fmtpServerSa;
                if (sa_new(&fmtpServerSa, fmtpAddrSpec, 0)) { // NB: port 0
                    log_add("Couldn't create service address for FMTP server");
                }
                else {
                    unsigned short vlanId;
                    if ((sscanf(vlanIdSpec, "%hu %n", &vlanId, &nbytes) != 1) ||
                            vlanIdSpec[nbytes] != 0) {
                        log_add("Couldn't parse VLAN ID specification \"%s\"",
                                vlanIdSpec);
                    }
                    else {
                        CidrAddr* fmtpSubnet = cidrAddr_parse(fmtpSubnetSpec);
                        if (fmtpSubnet == NULL) {
                            log_add("Couldn't parse FMTP subnet \"%s\"",
                                    fmtpSubnetSpec);
                            status = LDM7_INVAL;
                        }
                        else {
                            McastInfo* mcastInfo;
                            status = mi_new(&mcastInfo, feed, mcastGroupSa,
                                    fmtpServerSa);
                            if (0 == status) {
                                VcEndPoint* vcEnd = vcEndPoint_new(vlanId,
                                        switchSpec, switchPortSpec);
                                if (vcEnd == NULL) {
                                    log_add("Couldn't construct virtual-"
                                            "circuit endpoint");
                                    status = LDM7_SYSTEM;
                                }
                                else {
                                    struct in_addr mcastIface;
                                    status = inet_pton(AF_INET, mcastIfaceSpec, 
                                            &mcastIface);
                                    if (status != 1) {
                                        log_add("Couldn't decode multicast "
                                                "interface specification "
                                                "\"%s\"", mcastIfaceSpec);
                                        status = LDM7_INVAL;
                                    }
                                    else {
                                        status = lcf_addMulticast(mcastIface,
                                            mcastInfo, ttl, vcEnd, fmtpSubnet,
                                            getQueuePath());
                                    }
                                    vcEndPoint_free(vcEnd);
                                } // `vcEnd` allocated
                                mi_free(mcastInfo);
                            } // `mcastInfo` allocated
                            cidrAddr_delete(fmtpSubnet);
                        } // `fmtpSubnet` set
                    } // `vlanId` set
                    sa_free(fmtpServerSa);
                } // `fmtpServerSa` allocated
            } // `ttl` set
            sa_free(mcastGroupSa);
        } // `mcastGroupSa` allocated
    } // `feed` set
    
    return status;
}

/**
 * Decodes a RECEIVE entry.
 *
 * @param[in] feedtypeSpec   Specification of feedtype.
 * @param[in] LdmServerSpec  Specification of upstream LDM server.
 * @param[in] fmtpIface      Name of interface to be created and used by FMTP
 *                           layer (e.g., "ens33.232")
 * @param[in] switchId       Local AL2S switch ID
 * @param[in] portId         Port on local AL2S switch
 * @param[in] al2sVlanId     VLAN ID to/from local AL2S switch
 * @retval    0              Success.
 * @retval    EINVAL         Invalid specification. `log_add()` called.
 * @retval    ENOMEM         Out-of-memory. `log_add()` called.
 */
static int
processReceiveEntry(
        const char* const restrict feedtypeSpec,
        const char* const restrict ldmServerSpec,
        const char* const restrict fmtpIface,
        const char* const restrict switchId,
        const char* const restrict portId,
        const char* const restrict al2sVlanId)
{
    feedtypet   feedtype;
    int         status = decodeFeedtype(&feedtype, feedtypeSpec);

    if (0 == status) {
        ServiceAddr* ldmSvcAddr;

        status = sa_parseWithDefaults(&ldmSvcAddr, ldmServerSpec, NULL,
                ldmPort);       // Internet ID must exist; port is optional

        if (status) {
            log_add("Couldn't parse receive entry");
        }
        else {
            status = lcf_addReceive(feedtype, ldmSvcAddr, fmtpIface,
                    switchId, portId, al2sVlanId);

            if (status)
                log_add("Couldn't add RECEIVE entry");
            sa_free(ldmSvcAddr);
        } // `ldmSvcAddr` allocated
    } // `feedtype` set

    return status;
}
#endif // WANT_MULTICAST


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
                        if (execute)
                            error = lcf_addExec(&words);
                        
                        if (!execute || error)
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
                    int errCode = processReceiveEntry($2, $3, "dummy", "dummy",
                            "dummy", "dummy");

                    if (errCode) {
                        log_add("Couldn't process receive entry "
                                "\"RECEIVE %s %s %s\"", $2, $3);
                        return errCode;
                    }
                #endif
                }
                | RECEIVE_K STRING STRING STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = processReceiveEntry($2, $3, $4, $5, $6,
                            "dummy");

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
                    int errCode = processReceiveEntry($2, $3, $4, $5, $6, $7);

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

multicast_entry: MULTICAST_K STRING STRING STRING STRING STRING STRING STRING STRING
                {
                #if WANT_MULTICAST
                    int errCode = decodeMulticastEntry($2, $3, $4, $5, $6, $7,
                            $8, $9, "0.0.0.0");

                    if (errCode) {
                        log_add("Couldn't decode MULTICAST entry "
                                "\"MULTICAST %s %s %s %s %s %s %s\"",
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
                                "\"MULTICAST %s %s %s %s %s %s %s %s\"",
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

/**
 * Acts upon parsed REQUEST and RECEIVE entries of the configuration-file.
 *
 * @retval 0  Success
 * @return    System error code.
 */
static int
actUponEntries(
        const unsigned defaultPort)
{
    int status = lcf_startRequesters(defaultPort);

    if (status) {
        log_add("Problem starting downstream LDM-s");
    }
#if WANT_MULTICAST
    else {
        status = d7mgr_startAll();

        if (status) {
            log_add("Couldn't start all multicast LDM receivers");
            d7mgr_free();
        }
    }
#endif
    
    return status;
}

/**
 * Parses an LDM configuration-file and optionally executes the entries.
 * 
 * @param[in] pathname          Pathname of configuration-file.
 * @param[in] execEntries       Whether or not to execute the entries.
 * @param[in] ldmAddr           LDM server IP address in network byte order.
 * @param[in] defaultPort       The default LDM port.
 * @retval    0                 Success.
 * @retval    -1                Failure.  `log_add()` called.
 */
int
read_conf(
    const char* const   pathname,
    int                 execEntries,
    in_addr_t           ldmAddr,
    unsigned            defaultPort)
{
    int status;

    if (scannerPush(pathname)) {
        log_add("Couldn't open LDM configuration-file \"%s\"", pathname);
        status = -1;
    }
    else {
        ldmPort = defaultPort;
        execute = execEntries;
        ldmIpAddr = ldmAddr;
        // yydebug = 1;
        status = yyparse();

        if (status) {
            log_add("Couldn't parse LDM configuration-file \"%s\"", pathname);
            status = -1;
        }
        else if (execute) {
            status = actUponEntries(defaultPort) ? -1 : 0;
        }
    }

    return status;
}
