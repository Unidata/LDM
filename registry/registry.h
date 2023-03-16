/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This header-file specifies the API for the registry.
 *   The methods of this module are thread-compatible but not thread-safe.
 *
 *   The registry comprises a directed acyclic graph of nodes.  Each node is
 *   either a map-node or a value-node.  A value-node comprises a value; a
 *   map-node comprises a mapping from names to nodes.
 */
/* Apparently, the macro REGISTRY_H is defined by the compilation system */
#ifndef LDM_REGISTRY_H
#define LDM_REGISTRY_H

#define REG_SEP         "/"
#define REG_DELETE_INFO_FILES "/delete-info-files"
#define REG_HOSTNAME "/hostname"
#define REG_INSERTION_CHECK_INTERVAL "/insertion-check-interval"
#define REG_RECONCILIATION_MODE "/reconciliation-mode"
#define REG_CHECK_TIME "/check-time/enabled"
#define REG_CHECK_TIME_LIMIT "/check-time/limit"
#define REG_WARN_IF_CHECK_TIME_DISABLED "/check-time/warn-if-disabled"
#define REG_NTPDATE_COMMAND "/check-time/ntpdate/command"
#define REG_NTPDATE_SERVERS "/check-time/ntpdate/servers"
#define REG_NTPDATE_TIMEOUT "/check-time/ntpdate/timeout"
#define REG_LOG_COUNT "/log/count"
#define REG_LOG_FILE "/log/file"
#define REG_LOG_ROTATE "/log/rotate"
#define REG_METRICS_COUNT "/metrics/count"
#define REG_METRICS_FILE "/metrics/file"
#define REG_METRICS_FILES "/metrics/files"
#define REG_NETSTAT_COMMAND "/metrics/netstat-command"
#define REG_TOP_COMMAND "/metrics/top-command"
#define REG_PQACT_CONFIG_PATH "/pqact/config-path"
#define REG_PQACT_DATADIR_PATH "/pqact/datadir-path"
#define REG_PQSURF_CONFIG_PATH "/pqsurf/config-path"
#define REG_PQSURF_DATADIR_PATH "/pqsurf/datadir-path"
#define REG_QUEUE_PATH "/queue/path"
#define REG_QUEUE_SIZE "/queue/size"
#define REG_QUEUE_SLOTS "/queue/slots"
#define REG_SCOUR_CONFIG_PATH "/scour/config-path"
#define REG_SCOUR_EXCLUDE_PATH "/scour/exclude-path"
#define REG_LDMD_CONFIG_PATH "/server/config-path"
#define REG_IP_ADDR "/server/ip-addr"
#define REG_MAX_CLIENTS "/server/max-clients"
#define REG_MAX_LATENCY "/server/max-latency"
#define REG_PORT "/server/port"
#define REG_TIME_OFFSET "/server/time-offset"
#define REG_ANTI_DOS "/server/enable-anti-DOS"
#define REG_SURFQUEUE_PATH "/surf-queue/path"
#define REG_SURFQUEUE_SIZE "/surf-queue/size"
#define REG_OESS_PATHNAME "/oess-pathname"
#define REG_RETX_TIMEOUT "/fmtp-retx-timeout"

typedef int                     RegStatus;

#include <errno.h>
#include <timestamp.h>
#include <ldm.h>
#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sets the pathname of the directory that contains the registry.  To have an
 * effect, this function must be called before any function that accesses the
 * registry and after calling "reg_reset()".
 *
 * Arguments:
 *      path            Pointer to the pathname of the parent directory of the
 *                      registry.  May be NULL.  If NULL, then the default
 *                      pathname is used.  The client may free upon return.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           Backend database already open.  "log_add()" called.
 */
RegStatus reg_setDirectory(
    const char* const   path);

/*
 * Closes the registry.  Frees all resources and unconditionally resets the
 * module (excluding the pathname of the registry).
 *
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus reg_close(void);

/*
 * Resets the registry if it exists.  Unconditionally resets this module.
 * Doesn't return the pathname of the database to its default value.
 *
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_reset(void);

/*
 * Removes the registry if it exists.  Unconditionally resets this module.
 *
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_remove(void);

/*
 * Returns the string representation of a value from the registry.  The value
 * is obtained from the backing store.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      returned.  Shall not be NULL. Shall not contain a space.
 *      value           Pointer to a pointer to the value.  Shall not be NULL.
 *                      Set upon successful return.  The client should call
 *                      "free(*value)" when the value is no longer needed.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      ENOENT          No such value.  "log_add()" called.
 *      EINVAL          "path" contains a space.
 *      EINVAL          The path name isn't absolute.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_getString(
    const char* const   path,
    char** const        value);

/*
 * Returns a value from the registry as a boolean.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      returned.  Shall not be NULL. Shall not contain a space.
 *      value           Pointer to memory to hold the value.  Shall not be
 *                      NULL.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      ENOENT          No such value.  "log_add()" called.
 *      EINVAL          The path name isn't absolute.  "log_add()" called.
 *      EINVAL          "path" contains a space.
 *      EILSEQ          The value found isn't a boolean. "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_getBool(
    const char* const   path,
    unsigned* const     value);

/*
 * Returns a value from the registry as an unsigned integer.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      returned.  Shall not be NULL. Shall not contain a space.
 *      value           Pointer to memory to hold the value.  Shall not be
 *                      NULL.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      ENOENT          No such value.  "log_add()" called.
 *      EINVAL          The path name isn't absolute.  "log_add()" called.
 *      EINVAL          "path" contains a space.
 *      EILSEQ          The value found isn't an unsigned integer.
 *                      "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_getUint(
    const char* const   path,
    unsigned* const     value);

/*
 * Returns a value from the registry as a time.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      returned.  Shall not be NULL. Shall not contain a space.
 *      value           Pointer to memory to hold the value.  Shall not be NULL.
 *                      The client may free upon return.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      ENOENT          No such value.  "log_add()" called.
 *      EINVAL          The path name isn't absolute.  "log_add()" called.
 *      EINVAL          "path" contains a space.
 *      EILSEQ          The value found isn't a timestamp.  "log_add()"
 *                      called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_getTime(
    const char* const           path,
    timestampt* const           value);

/*
 * Returns a value from the registry as a signature.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      returned.  Shall not be NULL. Shall not contain a space.
 *      value           Pointer to memory to hold the value.  Shall not be NULL.
 *                      The client may free upon return.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      ENOENT          No such value.  "log_add()" called.
 *      EINVAL          The path name isn't absolute.  "log_add()" called.
 *      EINVAL          "path" contains a space.
 *      EILSEQ          The value found isn't a signature.  "log_add()"
 *                      called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_getSignature(
    const char* const           path,
    signaturet* const           value);

/*
 * Puts a boolean value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist. Shall not contain a space.
 *      value           The value to be written to the registry.
 * Returns:
 *      0               Success.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          A node or value would have to be created with the same
 *                      absolute path name as an existing value or node.
 *                      "log_add()" called.
 */
RegStatus reg_putBool(
    const char* const   path,
    const int           value);

/*
 * Puts an unsigned integer value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist. Shall not contain a space.
 *      value           The value to be written to the registry.
 * Returns:
 *      0               Success.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          A node or value would have to be created with the same
 *                      absolute path name as an existing value or node.
 *                      "log_add()" called.
 */
RegStatus reg_putUint(
    const char* const   path,
    const unsigned      value);

/*
 * Puts a string value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist. Shall not contain a space.
 *      value           Pointer to the value to be written to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          A node or value would have to be created with the same
 *                      absolute path name as an existing value or node.
 *                      "log_add()" called.
 */
RegStatus reg_putString(
    const char* const   path,
    const char* const   value);

/*
 * Puts a time value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist. Shall not contain a space.
 *      value           Pointer to the value to be written to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          A node or value would have to be created with the same
 *                      absolute path name as an existing value or node.
 *                      "log_add()" called.
 */
RegStatus reg_putTime(
    const char* const           path,
    const timestampt* const     value);

/*
 * Puts a signature value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist. Shall not contain a space.
 *      value           Pointer to the value to be written to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          A node or value would have to be created with the same
 *                      absolute path name as an existing value or node.
 *                      "log_add()" called.
 */
RegStatus reg_putSignature(
    const char* const   path,
    const signaturet    value);

/*
 * Deletes a value from the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      deleted.  Shall not be NULL. Shall not contain a space.
 * Returns:
 *      0               Success
 *      ENOENT          No such value
 *      EINVAL          The absolute path name is invalid.  "log_add()"
 *                      called.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_deleteValue(
    const char* const   path);

/*
 * Returns a node in the registry.  Can create the node and its ancestors if
 * they don't exist.  If the node didn't exist, then changes to the node won't
 * be made persistent until "reg_flush()" is called on the node or one of its
 * ancestors.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the node to be
 *                      returned.  Shall not be NULL.  The empty string obtains
 *                      the top-level node. Shall not contain a space.
 *      node            Pointer to a pointer to a node.  Shall not be NULL.
 *                      Set on success.  The client should call
 *                      "rn_free(*node)" when the node is no longer
 *                      needed.
 *      create          Whether or not to create the node if it doesn't
 *                      exist.  Zero means no; otherwise, yes.
 * Returns:
 *      0               Success.  "*node" is set.  The client should call
 *                      "rn_free(*node)" when the node is no longer
 *                      needed.
 *      ENOENT          "create" was 0 and the node doesn't exist. "log_add()"
 *                      called.
 *      EINVAL          "path" isn't a valid absolute path name.  "log_add()
 *                      called.
 *      EINVAL          "path" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_getNode(
    const char* const   path,
    RegNode** const     node,
    const int           create);

/*
 * Deletes a node and all of its children.  The node and its children are only
 * marked as being deleted: they are not removed from the registry until
 * "reg_flushNode()" is called on the node or one of its ancestors.
 *
 * Arguments:
 *      node            Pointer to the node to be deleted along with all it
 *                      children.  Shall not be NULL.
 */
void reg_deleteNode(
    RegNode*    node);

/*
 * Flushes all changes to a node and its children to the backend database.
 *
 * Arguments:
 *      node            Pointer to the node to be flushed to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_flushNode(
    RegNode*    node);

/*
 * Returns the name of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose name will be returned.  Shall
 *                      not be NULL.
 * Returns:
 *      Pointer to the name of the node.  The client shall not free.
 */
const char* reg_getNodeName(
    const RegNode* const        node);

/*
 * Returns the absolute path name of a node.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 * Returns:
 *      Pointer to the absolute path name of the node.  The client shall not
 *      free.
 */
const char* reg_getNodeAbsPath(
    const RegNode* const        node);

/*
 * Adds a string value to a node.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be
 *                      NULL.  The client may free upon return. Shall not
 *                      contain a space.
 *      value           Pointer to the string value.  Shall not be NULL.  The
 *                      client may free upon return.
 * Returns:
 *      0               Success
 *      EINVAL          "path" contains a space.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
RegStatus reg_putNodeString(
    RegNode*            node,
    const char*         name,
    const char*         value);

/*
 * Adds a boolean value to a node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return. Shall not
 *                      contain a space.
 *      value           The boolean value
 * Returns:
 *      0               Success
 *      EINVAL          "path" contains a space.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
RegStatus reg_putNodeBool(
    RegNode*            node,
    const char*         name,
    int                 value);

/*
 * Adds an unsigned integer value to a node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return. Shall not
 *                      contain a space.
 *      value           The unsigned integer value
 * Returns:
 *      0               Success
 *      EINVAL          "path" contains a space.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
RegStatus reg_putNodeUint(
    RegNode*            node,
    const char*         name,
    unsigned            value);

/*
 * Adds a time value to a node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return. Shall not 
 *                      contain a space.
 *      value           Pointer to the time value.  Shall not be NULL.
 *                      The client may free upon return.
 * Returns:
 *      0               Success
 *      EINVAL          "path" contains a space.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
RegStatus reg_putNodeTime(
    RegNode*            node,
    const char*         name,
    const timestampt*   value);

/*
 * Adds a signature value to a node.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be
 *                      NULL.  The client may free upon return. Shall not 
 *                      contain a space.
 *      value           Pointer to the signature value.  Shall not be NULL.
 *                      The client may free upon return.
 * Returns:
 *      0               Success
 *      EINVAL          "path" contains a space.
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
RegStatus reg_putNodeSignature(
    RegNode*            node,
    const char*         name,
    const signaturet    value);

/*
 * Returns a string value of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose value is to be returned
 *                      as a string.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *                      Shall not contain a space.
 *      value           Pointer to a pointer to a string.  Shall not be NULL.
 *                      Set upon successful return.  The client should call call
 *                      "free(*value)" when the value is no longer needed.
 * Returns:
 *      0               Success.  "*value" is set.
 *      EINVAL          "path" contains a space.
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node containing the value has been deleted.
 *                      "log_add()" called.
 */
RegStatus reg_getNodeString(
    const RegNode* const        node,
    const char* const           name,
    char** const                value);

/*
 * Returns an boolean value of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose value is to be returned.
 *                      Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *                      Shall not contain a space.
 *      value           Pointer to an integer.  Set upon success.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is set.
 *      EINVAL          "path" contains a space.
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EILSEQ          The value isn't a boolean.  "log_add()"
 *                      called.
 */
RegStatus reg_getNodeBool(
    const RegNode* const        node,
    const char* const           name,
    int* const                  value);

/*
 * Returns an unsigned integer value of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose value is to be returned.
 *                      Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *                      Shall not contain a space.
 *      value           Pointer to an unsigned integer.  Set upon success.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is set.
 *      EINVAL          "path" contains a space.
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EILSEQ          The value isn't an unsigned integer.  "log_add()"
 *                      called.
 */
RegStatus reg_getNodeUint(
    const RegNode* const        node,
    const char* const           name,
    unsigned* const             value);

/*
 * Returns a time value of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose value is to
 *                      be returned as a time.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *                      Shall not contain a space.
 *      value           Pointer to a time.  Set upon success.  Shall not be
 *                      NULL.  The client may free upon return.
 * Returns:
 *      0               Success.  "*value" is set.
 *      EINVAL          "path" contains a space.
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EILSEQ          The value isn't a time.  "log_add()" called.
 */
RegStatus reg_getNodeTime(
    const RegNode* const        node,
    const char* const           name,
    timestampt* const           value);

/*
 * Returns a signature value of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose value is to be returned as a
 *                      signature.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *      value           Pointer to a signature.  Set upon success.  Shall not
 *                      be NULL.  The client may free upon return.
 * Returns:
 *      0               Success.  "*value" is not NULL.
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EILSEQ          The value isn't a signature.  "log_add()" called.
 */
RegStatus reg_getNodeSignature(
    const RegNode* const        node,
    const char* const           name,
    signaturet* const           value);

/*
 * Deletes a value from a node.  The value is only marked as being deleted: it
 * is not removed from the registry until "reg_flushNode()" is called on the
 * node or one of its ancestors.
 *
 * Arguments:
 *      node            Pointer to the node to have the value deleted.  Shall
 *                      note be NULL.
 *      name            Pointer to the name of the value to be deleted.  Shall
 *                      not be NULL. Shall not contain a space.
 * Returns:
 *      0               Success
 *      EINVAL          "path" contains a space.
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node has been deleted.  "log_add()" called.
 */
RegStatus reg_deleteNodeValue(
    RegNode* const      node,
    const char* const   name);

/*
 * Visits a node and all its descendants in the natural order of their path
 * names.
 *
 * Arguments:
 *      node            Pointer to the node at which to start.  Shall not be
 *                      NULL.
 *      func            Pointer to the function to call at each node.  Shall
 *                      not be NULL.  The function shall not modify the set
 *                      of child-nodes to which the node being visited belongs.
 * Returns:
 *      0               Success
 *      else            The first non-zero value returned by "func".
 */
RegStatus reg_visitNodes(
    RegNode* const      node,
    const NodeFunc      func);

/*
 * Visits all the values of a node in the natural order of their path names.
 *
 * Arguments:
 *      node            Pointer to the node whose values are to be visited.
 *                      Shall not be NULL.
 *      func            Pointer to the function to call for each value.
 *                      Shall not be NULL.  The function shall not modify the
 *                      set of values to which the visited value belongs.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      else            The first non-zero value returned by "func"
 */
RegStatus reg_visitValues(
    RegNode* const      node,
    const ValueFunc     func);

#ifdef __cplusplus
}
#endif

#endif
