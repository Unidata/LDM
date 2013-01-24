/**
 *   Copyright 2013, University Corporation for Atmospheric Research.
 *   All Rights reserved.
 *   <p>
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
#ifndef _ACL_H
#define _ACL_H

#include <sys/types.h>
#include <regex.h>
#include "ldm.h"
#include "peer_info.h"
#include "wordexp.h"
#include "error.h"
#include "UpFilter.h"

#ifndef ENOERR
#define ENOERR 0
#endif

enum host_set_type { HS_NONE, HS_NAME, HS_DOTTED_QUAD, HS_REGEXP };
typedef struct {
	enum host_set_type type;
	const char *cp;	/* hostname or pattern */
	regex_t rgx; 
} host_set;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Saves information on the last, successfully-received product under a key
 * that comprises the relevant components of the data-request.
 */
void
savePreviousProdInfo(void);

/**
 * Frees a specification of a set of hosts.
 *
 * @param hsp       [in/out] The specification of a set of hosts.
 */
void
free_host_set(host_set *hsp);

/**
 * Returns a new specification of a set of hosts.
 *
 * @param cp    Pointer to host(s) specification.  Caller must not free on
 *              return if and only if call is successful and "type" is
 *              HS_REGEXP.
 * @param rgxp  Pointer to regular-expression structure.  Caller may free
 *              on return but must not call regfree() if and only if call is
 *              successful and "type" is HS_REGEXP.
 * @retval NULL Out of memory. No error-message logged or started.
 */
host_set *
new_host_set(enum host_set_type type, const char *cp, const regex_t *rgxp);

/**
 * Starts a process and adds it to the set of processes.
 *
 * @param words     [in] Command-line words. Client may free upon return.
 * @retval 0        Success.
 * @return          System error code.
 */
int
exec_add(wordexp_t *wrdexpp);

/**
 * Frees an entry in the process list.
 *
 * @param pid       [in] The process identifier of the child process whose
 *                  entry is to be freed.
 */
void
exec_free(
    const pid_t pid);

/**
 * Returns the command-line of a child process.
 *
 * @param pid       [in] The process identifier of the child process.
 * @param buf       [in] The buffer into which to write the command-line.
 * @param size      [in] The size of "buf".
 * @retval -2       The child process wasn't found.
 * @retval -1       Write error.  See errno.  Error-message(s) written via
 *                  log_*().
 * @return          The number of characters written into "buf" excluding any
 *                  terminating NUL.  If the number of characters equals "size",
 *                  then no terminating NUL was written.
 */
int
exec_getCommandLine(
    const pid_t         pid,
    char* const         buf,
    size_t              size);

/**
 * Adds a REQUEST entry.
 *
 * @param feedtype      [in] Feedtype.
 * @param pattern       [in] Pattern. Client may free upon return.
 * @param hostId        [in] Host identifier. Client may free upon return.
 * @param port          [in] Port number.
 * @param lineNo        [in] Line number of entry.
 * @param pathname      [in] Pathname of the configuration-file.
 * @retval 0            Success.
 * @retval -1           System error. log_add() called.
 */
int
acl_addRequest(
    const feedtypet     feedtype,
    const char* const   pattern,
    const char* const   hostId,
    const unsigned      port,
    const unsigned      lineNo,
    const char* const   pathname);

/**
 * Adds an ALLOW entry.
 *
 * @param ft            [in] The feedtype.
 * @param hostSet       [in] Pointer to allocated set of allowed downstream hosts.
 *                      Upon successful return, the client shall abandon
 *                      responsibility for calling "free_host_set(hostSet)".
 * @param okEre         [in] Pointer to the ERE that data-product identifiers
 *                      must match.  Caller may free upon return.
 * @param notEre        [in] Pointer to the ERE that data-product identifiers
 *                      must not match or NULL if such matching should be
 *                      disabled.  Caller may free upon return.
 * @retval NULL         Success.
 * @return              Failure error object.
 */
ErrorObj*
acl_addAllow(
    const feedtypet             ft,
    host_set* const             hostSet,
    const char* const           okEre,
    const char* const           notEre);

/**
 * Indicates if it's OK to feed or notify a given host a given class of
 * data-products.
 *
 * @param *rmtip        [in] Information on the remote host.  rmtip->clssp will
 *                      be set to the intersection unless there's an
 *                      error, or there are no matching host entries in the
 *                      ACL, or the intersection is the empty set, in which
 *                      case it will be unmodified.
 * @param *want         [in] The product-class that the host wants.
 * @retval 0            if successful.
 * @retval ENOMEM       if out-of-memory.
 * @retval EINVAL       if a regular expression of a product specification
 *                      couldn't be compiled.
 */
int
forn_acl_ck(peer_info *rmtip, prod_class_t *want);

/**
 * Returns the class of products that a host is allowed to receive based on the
 * host and the feed-types of products that it wants to receive.  The pointer
 * to the product-class structure will reference allocated space on success;
 * otherwise, it won't be modified.  The returned product-class may be the
 * empty set.  The client is responsible for invoking
 * free_prod_class(prod_class_t*) on a non-NULL product-class structure when it
 * is no longer needed.
 *
 * @param name          [in] Pointer to the name of the host.
 * @param addr          [in] Pointer to the IP address of the host.
 * @param want          [in] Pointer to the class of products that the host wants.
 * @param intersect     [out] Pointer to a pointer to the intersection of the
 *                      wanted product class and the allowed product class.
 *                      References allocated space on success; otherwise won't
 *                      be modified.  Referenced product-class may be empty.
 *                      On success, the caller should eventually invoke
 *                      free_prod_class(*intersect).
 * @retval 0            Success.
 * @retval EINVAL       The regular expression pattern of a
 *                      product-specification couldn't be compiled.
 * @retval ENOMEM       Out-of-memory.
 */
int
acl_product_intersection(
    const char           *name,
    const struct in_addr *addr, 
    const prod_class_t     *want,
    prod_class_t          **const intersect);

/**
 * Returns the product-class appropriate for filtering data-products on the
 * upstream LDM before sending them to the downstream LDM.
 *
 * @param name          [in] Pointer to the name of the downstream host.
 * @param addr          [in] Pointer to the IP address of the downstream host.
 * @param want          [in] Pointer to the class of products that the downstream
 *                      host wants.
 * @param filter        [out] Pointer to a pointer to the upstream filter.
 *                      *filter is set on and only on success.  Caller
 *                      should call upFilter_free(*filter). *filter is set to
 *                      NULL if and only if no data-products should be sent to
 *                      the downstream LDM.
 * @retval NULL         Success.
 * @return              Failure error object.
 */
ErrorObj*
acl_getUpstreamFilter(
    const char*                 name,
    const struct in_addr*       addr, 
    const prod_class_t*         want,
    UpFilter** const            upFilter);

/**
 * Adds an ACCEPT entry.
 *
 * @param ft            [in] Feedtype.
 * @param pattern       [in] Pointer to allocated memory containing extended
 *                      regular-expression for matching the product-identifier.
 *                      The client shall not free.
 * @param rgxp          [in] Pointer to allocated memory containing the
 *                      regular-expression structure for matching
 *                      product-identifiers.  The client shall not free.
 * @param hsp           [in] Pointer to allocated memory containing the host-set.
 *                      The client shall not free.
 * @param isPrimary     [in] Whether or not the initial data-product exchange-mode
 *                      is primary (i.e., uses HEREIS) or alternate (i.e., uses
 *                      COMINGSOON/BLKDATA).
 * @retval 0            Success
 * @retval !0           <errno.h> error-code
 */
int
accept_acl_add(
    feedtypet     ft,
    char*         pattern,
    regex_t*      rgxp,
    host_set*     hsp,
    int           isPrimary);

/**
 * Checks the access-control-list (ACL) for ACCEPT entries.
 *
 * @param rmtip         [in/out] Information on the remote host. May be
 *                      modified.
 * @param offerd        [in] The product-class that the remote host is offering
 *                      to send.
 * @retval 0            if successful.
 * @retval ENOMEM       if out-of-memory.
 */
int
hiya_acl_ck(peer_info *rmtip, prod_class_t *offerd);

/**
 * Determines the set of acceptable products given the upstream host and the
 * offered set of products.
 *
 * @param name          [in] Pointer to name of host.
 * @param addr          [in] Pointer to Internet address of host.
 * @param dotAddr       [in] Pointer to the dotted-quad form of the IP address
 *                      of the host.
 * @param offered       [in] Pointer to the class of offered products.
 * @param accept        [out] Address of pointer to set of acceptable products.
 *                      On success, the pointer will be set to reference
 *                      allocated space; otherwise, it won't be modified.
 *                      Acceptable product set may be empty. The client should
 *                      call "free_prod_class(prod_class_t*)" when the
 *                      product-class is no longer needed.  In general, the
 *                      class of acceptable products will be a subset of
 *                      *offered.
 * @param isPrimary     [in] Pointer to flag indicating whether or not the
 *                      data-product exchange-mode should be primary (i.e., use
 *                      HEREIS) or alternate (i.e., use COMINGSOON/BLKDATA).
 * @retval 0            Success.
 * @retval ENOMEM       Failure.  Out-of-memory.
 */
int
acl_check_hiya(
    const char*         name,
    const char*         dotAddr, 
    prod_class_t*       offerd,
    prod_class_t**      accept,
    int*                isPrimary);

/**
 * Starts the necessary downstream LDM-s.
 *
 * @param ldmPort       [in] Ignored.
 * @retval 0            Success.
 * @return              System error code. log_add() called.
 */
int
invert_request_acl(
    unsigned    ldmPort);

/**
 * Indicates if a given host is allowed to connect in any fashion. First line
 * of (weak) defense.
 * <p>
 * Of course, a serious threat would spoof the IP address or name service.
 *
 * @retval 0        Iff the host is not allowed to connect.
 */
int
host_ok(const peer_info *rmtip);

/**
 * Frees this module's resources.
 */
void
acl_free(void);

#ifdef __cplusplus
}
#endif

#endif /* !_ACL_H */
