/*
 * This file implements the API for the registry.  It hides the decision on how
 * to implement the persistent store. The functions in this file are
 * thread-compatible but not thread-safe.
 *
 * Copyright 2019, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */
#include <config.h>

#undef NDEBUG

#include "backend.h"
#include "globals.h"
#include "ldmprint.h"
#include "log.h"
#include "misc.h"
#include "node.h"
#include "registry.h"
#include "string_buf.h"
#include "timestamp.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <search.h>

#define NOT_SYNCHED     0
#define SYNCHED         1

/*
 * Parses a string into a value.
 *
 * Arguments:
 *      string          Pointer to the string to be "parsed".  Shall not be
 *                      NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EILSEQ          The string isn't a valid representation of the type
 */
typedef RegStatus (*Parser)(const char* string, void* value);
/*
 * Formats a value into a string.
 *
 * Arguments:
 *      value           Pointer to the value to be formatted.  Shall not be
 *                      NULL.
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*strBuf" is not NULL.
 *      ENOMEM          System error.  "log_add()" called.
 */
typedef RegStatus (*Formatter)(const void* value, StringBuf* strBuf);

typedef enum {
    /* Start with 0 to enable use as an index: */
    REG_STRING = 0,
    REG_BOOL,
    REG_UINT,
    REG_TIME,
    REG_SIGNATURE,
    /* Keep at end to track the number of types: */
    REG_TYPECOUNT
}       RegType;

typedef struct {
    Parser      parse;
    Formatter   format;
}       TypeStruct;

static bool        _freeRegDir;      ///< free registry directory path?
static char*       _registryDir;     ///< registry directory path
static int         _initialized;     ///< Module is initialized?
static int         _atexitCalled;    ///< atexit() called?
static Backend*    _backend;         ///< backend database
static int         _forWriting;      ///< registry open for writing?
static StringBuf*  _pathBuf;         ///< buffer for creating paths
static StringBuf*  _formatBuf;       ///< values-formatting buffer
static RegNode*    _rootNode;        ///< root node of the registry tree
static ValueFunc   _extantValueFunc; ///< function when visiting nodes
static const char* _nodePath;        ///< pathname of visited node
static StringBuf*  _valuePath;       ///< pathname of visited value

/******************************************************************************
 * Private Functions:
 ******************************************************************************/

/**
 * Returns the registry directory pathname.
 *
 * Returns:
 *      The registry directory pathname.
 */
static const char* getRegistryDir(void)
{
    if (NULL == _registryDir) {
        char* const envPath = getenv("LDM_REGISTRY_DIR");
        _registryDir = envPath
                ? envPath
                : (char*)getRegistryDirPath();
        _freeRegDir = false;
    }
    return _registryDir;
}

/**
 * Frees the registry directory pathname, if necessary.
 */
static void freeRegistryDir(void)
{
    if (_freeRegDir) {
        free(_registryDir);
        _freeRegDir = false;
    }
    _registryDir = NULL;
}

/**
 * Sets the registry directory pathname.
 *
 * @retval 0            Success
 * @retval ENOMEM       System error.  "log_add()" called.
 */
static int setRegistryDir(
    const char* const   path)   /**< [in] Pointer to pathname of registry
                                  *  directory or NULL to reset the registry
                                  *  directory pathname to the default value. */
{
    int         status;

    if (NULL == path) {
        freeRegistryDir();
        (void)getRegistryDirPath();

        status = 0;
    }
    else {
        char*   clone;

        if (0 != (status = reg_cloneString(&clone, path))) {
            log_add("Couldn't set new registry pathname to \"%s\"", path);
        }
        else {
            freeRegistryDir();

            _registryDir = clone;
            _freeRegDir = true;
        }
    }

    return status;
}

/*
 * Resets this module
 */
static void resetRegistry(void)
{
    if (NULL != _pathBuf) {
        sb_free(_pathBuf);
        _pathBuf = NULL;
    }
    if (NULL != _formatBuf) {
        sb_free(_formatBuf);
        _formatBuf = NULL;
    }
    if (NULL != _valuePath) {
        sb_free(_valuePath);
        _valuePath = NULL;
    }
    if (NULL != _rootNode) {
        rn_free(_rootNode);
        _rootNode = NULL;
    }
    _initialized = 0;
    _backend = NULL;
    _forWriting = 0;
}

/*
 * Closes the registry if it's open.  Doesn't reset this module, however.
 *
 * Returns:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus closeRegistry(void)
{
    RegStatus   status = beClose(_backend);

    _backend = NULL;

    return status;
}

/*
 * Closes the registry if it's open.  This function shall only be called by
 * exit().
 */
static void terminate(void)
{
    (void)closeRegistry();
    resetRegistry();

    freeRegistryDir();
}

/*
 * "Parses" a string into a string.
 *
 * Arguments:
 *      string          Pointer to the string to be "parsed".  Shall not be
 *                      NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 */
static RegStatus parseString(
    const char* const   string,
    void* const         value)
{
    return reg_cloneString((char**)value, string);
}

/*
 * "Formats" a string into a string.
 *
 * Arguments:
 *      value           Pointer to the value to be formatted.  Shall not be
 *                      NULL.
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  The string-buffer is not NULL.
 *      ENOMEM   System error.  "log_add()" called.
 */
static RegStatus formatString(
    const void* const   value,
    StringBuf* const    strBuf)
{
    return sb_set(strBuf, (const char*)value, NULL);
}

/*
 * Parses a string into a boolean.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EILSEQ          The string doesn't represent a boolean.
 *                      "log_add()" called.
 */
static RegStatus parseBool(
    const char* const   string,
    void* const         value)
{
    RegStatus           status;

    if (strcasecmp(string, "TRUE") == 0 || strcasecmp(string, "YES") == 0 ||
            strcasecmp(string, "AFIRMATIVE") == 0 || strcmp(string, "1") == 0) {
        *(int*)value = 1;
        status = 0;
    }
    else {
        if (strcasecmp(string, "FALSE") == 0 || strcasecmp(string, "NO") == 0 ||
                strcasecmp(string, "NEGATIVE") == 0 || strcmp(string, "0") == 0) {
            *(int*)value = 0;
            status = 0;
        }
        else {
            char* end;
            long  val;

            errno = 0;
            val = strtol(string, &end, 0);

            if (0 != *end || (0 == val && 0 != errno)) {
                log_add("Not a boolean: \"%s\"", string);
                status = EILSEQ;
            }
            else {
                *(int*)value = val != 0;
                status = 0;
            }
        }
    }

    return status;
}

/*
 * Formats a boolean into a string.
 *
 * Arguments:
 *      value           Pointer to the boolean to be formatted
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is not NULL.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus formatBool(
    const void* const   value,
    StringBuf* const    strBuf)
{
    static char buf[6];

    (void)snprintf(buf, sizeof(buf), "%s", *(int*)value ? "TRUE" : "FALSE");

    return sb_set(strBuf, buf, NULL);
}

/*
 * Parses a string into an unsigned integer.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EILSEQ          The string doesn't represent an unsigned integer.
 *                      "log_add()" called.
 */
static RegStatus parseUint(
    const char* const   string,
    void* const         value)
{
    RegStatus           status;
    char*               end;
    unsigned long       val;

    errno = 0;
    val = strtoul(string, &end, 0);

    if (0 != *end || (0 == val && 0 != errno)) {
        log_add("Not an unsigned integer: \"%s\"", string);
        status = EILSEQ;
    }
    else {
        *(unsigned*)value = (unsigned)val;
        status = 0;
    }

    return status;
}

/*
 * Formats an unsigned integer into a string.
 *
 * Arguments:
 *      value           Pointer to the value to be formatted
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is not NULL.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus formatUint(
    const void* const   value,
    StringBuf* const    strBuf)
{
    static char buf[80];

    (void)snprintf(buf, sizeof(buf), "%u", *(unsigned*)value);

    return sb_set(strBuf, buf, NULL);
}

/*
 * Parses a string into a time.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *                      The format of the string shall be
 *                      YYYYMMDDThhmmss[.uuuuuu].
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EILSEQ          The string doesn't represent a time.  "log_add()"
 *                      called.
 */
static RegStatus parseTime(
    const char* const   string,
    void* const         value)
{
    RegStatus   status;
    timestampt  val;

    status = tsParse(string, &val);

    if (0 > status || 0 != string[status]) {
        log_add("Not a timestamp: \"%s\"", string);
        status = EILSEQ;
    }
    else {
        *(timestampt*)value = val;
        status = 0;
    }

    return status;
}

/*
 * Formats a time into a string.
 *
 * Arguments:
 *      value           Pointer to the value to be formatted
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*strBuf" is not NULL.
 *      ENOMEM   System error.  "log_add()" called.
 */
static RegStatus formatTime(
    const void* const   value,
    StringBuf* const    strBuf)
{
    return sb_set(strBuf, tsFormat((const timestampt*)value), NULL);
}

/*
 * Parses a string into a signature.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EILSEQ          The string doesn't represent a signature
 */
static RegStatus parseSignature(
    const char* const   string,
    void* const         value)
{
    RegStatus   status;
    signaturet  val;

    status = sigParse(string, &val);

    if (0 > status || 0 != string[status]) {
        log_add("Not a signature: \"%s\"", string);
        status = EILSEQ;
    }
    else {
        (void)memcpy(value, val, sizeof(signaturet));
        status = 0;
    }

    return status;
}

/*
 * Formats a signature into a string.
 *
 * Arguments:
 *      value           Pointer to the value to be formatted
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*strBuf" is not NULL.
 *      ENOMEM   System error.  "log_add()" called.
 */
static RegStatus formatSignature(
    const void* const   value,
    StringBuf* const    strBuf)
{
    return sb_set(strBuf, s_signaturet(NULL, 0, value),
        NULL);
}

static const TypeStruct  stringStruct = {parseString, formatString};
static const TypeStruct  boolStruct = {parseBool, formatBool};
static const TypeStruct  uintStruct = {parseUint, formatUint};
static const TypeStruct  timeStruct = {parseTime, formatTime};
static const TypeStruct  signatureStruct = {parseSignature, formatSignature};

/*
 * Synchronizes a node and its descendants from the backend database.
 *
 * Arguments:
 *      node            Pointer to the node to have it and its descendants
 *                      synchronized from the backend database.  Shall not be
 *                      NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus sync(
    RegNode* const      node)
{
    const char* absPath = rn_getAbsPath(node);
    RegStatus   status;

    rn_clear(node);

    if (0 == (status = beInitCursor(_backend))) {
        for (status = beFirstEntry(_backend, absPath); 0 == status;
                status = beNextEntry(_backend)) {
            const char* key = beGetKey(_backend);

            if (strstr(key, absPath) != key) {
                /* The entry is outside the scope of "node" */
                break;
            }
            else {
                char* relPath;
                char* name;

                if (0 == (status = reg_splitAbsPath(key, absPath, &relPath,
                        &name))) {
                    RegNode*        subnode;

                    if (0 == (status = rn_ensure(node, relPath, &subnode))) {
                        ValueThing* vt;
                        const char*     value = beGetValue(_backend);

                        if (0 == (status = rn_putValue(subnode, name, value,
                                &vt))) {
                            (void)vt_setStatus(vt, SYNCHED);
                        }
                    }

                    free(relPath);
                    free(name);
                }                       /* "relPath" & "name" allocated */
            }
        }

        if (ENOENT == status)
            status = 0;

        beFreeCursor(_backend);
    }                                   /* cursor initialized */

    if (status)
        log_add("Couldn't synchronize node \"%s\"", absPath);

    return status;
}

/*
 * Initializes the registry.  Ensures that the backend is open for the desired
 * access.  Registers the process termination function.  May be called many
 * times.
 *
 * Arguments:
 *      forWriting      Indicates if the backend should be open for writing:
 *                      0 <=> no.
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus initRegistry(
    const int   forWriting)
{
    RegStatus   status = 0;             /* success */
 
    if (!_initialized) {
        if (0 != sb_new(&_pathBuf, 80)) {
            log_add("Couldn't allocate path-buffer");
            status = ENOMEM;
        }
        else {
            if (0 != sb_new(&_formatBuf, 80)) {
                log_add("Couldn't allocate formating-buffer");
                status = ENOMEM;
            }
            else {
                if (0 != sb_new(&_valuePath, 80)) {
                    log_add("Couldn't allocate value-pathname buffer");
                    status = ENOMEM;
                }
                else {
                    _initialized = 1;
                    status = 0;
                }                   /* "_valuePath" allocated */

                if (status) {
                    sb_free(_formatBuf);
                    _formatBuf = NULL;
                }
            }                       /* "_formatBuf" allocated */

            if (status) {
                sb_free(_pathBuf);
                _pathBuf = NULL;
            }
        }                           /* "_pathBuf" allocated */
    }                               /* module not initialized */
 
    if (0 == status) {
        if (NULL != _backend && forWriting && !_forWriting) {
            /* The backend is open for the wrong (read-only) access */
            status = beClose(_backend);
            _backend = NULL;
        }
 
        if (0 == status && NULL == _backend) {
            /* The backend isn't open. */
            if (0 != (status = beOpen(&_backend, getRegistryDir(),
                            forWriting))) {
                log_add("Couldn't open registry");
            }
            else {
                _forWriting = forWriting;
 
                if (NULL == _rootNode) {
                    RegNode*    root;

                    if (0 == (status = rn_newRoot(&root))) {
                        if (0 != (status = sync(root))) {
                            rn_free(root);
                        }
                        else {
                            _rootNode = root;
                        }
                    }                   /* "root" allocated */
                }
                if (status) {
                    beClose(_backend);
                    _backend = NULL;
                }
            }                           /* "_backend" allocated and open */
        }                               /* backend database not open */
    }                                   /* module initialized */

    if (!_atexitCalled) {
        if (0 == atexit(terminate)) {
            _atexitCalled = 1;
        }
        else {
            log_syserr("Couldn't register registry cleanup routine");
        }
    }
 
    return status;
}

/*
 * Forms the absolute path name of a value.
 *
 * Arguments:
 *      sb              Pointer to a string-buffer.  Shall not be NULL.
 *      nodePath        Pointer to the absolute path name of the containing
 *                      node.  Shall not be NULL.
 *      vt              Pointer to the value-thing.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 */
static int formAbsValuePath(
    StringBuf* const            sb,
    const char* const           nodePath,
    const ValueThing* const     vt)
{
    return sb_set(sb, reg_isAbsRootPath(nodePath) ? "" : nodePath, REG_SEP,
        vt_getName(vt), NULL);
}

/*
 * Writes a value to the backend database.
 *
 * Arguments:
 *      vt              Pointer to the ValueThing.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus writeValue(
    ValueThing* const   vt)
{
    RegStatus   status;

    if (SYNCHED == vt_getStatus(vt)) {
        status = 0;
    }
    else {
        if (0 == (status = formAbsValuePath(_valuePath, _nodePath, vt))) {
            if (0 == (status = bePut(_backend, sb_string(_valuePath),
                    vt_getValue(vt)))) {
                (void)vt_setStatus(vt, SYNCHED);
            }
        }
    }

    return status;
}

/*
 * Deletes a value from the backend database.
 *
 * Arguments:
 *      vt              Pointer to the ValueThing.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus deleteValue(
    ValueThing* const     vt)
{
    RegStatus   status = formAbsValuePath(_valuePath, _nodePath, vt);

    if (0 == status)
        status = beDelete(_backend, sb_string(_valuePath));

    return status;
}

/*
 * Writes a node to the backend database.
 *
 * Arguments:
 *      node            Pointer to the node to be written.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus writeNode(
    RegNode*    node)
{
    RegStatus   status = 0;                     /* success */

    if (rn_isDeleted(node)) {
        status = beDelete(_backend, rn_getAbsPath(node));
        _extantValueFunc = deleteValue;
    }

    if (0 == status) {
        _nodePath = rn_getAbsPath(node);

        if (0 == (status = rn_visitValues(node, _extantValueFunc, deleteValue)))
            rn_freeDeletedValues(node);

        if (0 != status)
            log_add("Couldn't update values of node \"%s\"",
                    rn_getAbsPath(node));
    }

    return status;
}

/*
 * Flushes a node and all its descendents to the backend database.
 *
 * Arguments:
 *      node            Pointer to the node to be flushed along with all its
 *                      descendents.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus flush(
    RegNode* const      node)
{
    _extantValueFunc = writeValue;

    return rn_visitNodes(node, writeNode);
}

/*
 * Returns a binary value of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose value is to be returned.
 *                      Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *      value           Pointer to the location to hold the binary value.
 *                      Shall not be NULL.
 *      typeStruct      Pointer to type-specific functions.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is set.
 *      ENOENT          No such value.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node containing the value has been deleted.
 *                      "log_add()" called.
 */
static RegStatus getNodeValue(
    const RegNode* const        node,
    const char* const           name,
    void* const                 value,
    const TypeStruct* const     typeStruct)
{
    RegStatus   status = initRegistry(0);

    if (0 == status) {
        char*       string;

        if (0 == (status = rn_getValue(node, name, &string))) {
            status = typeStruct->parse(string, value);
            free(string);
        }                               /* "string" allocated */
    }

    return status;
}

/*
 * Returns the binary representation of a value from the registry.
 *
 * Preconditions:
 *      The registry has been initialized.
 * Arguments:
 *      path            Pointer to the absolute path name of the value to be
 *                      returned.  Shall not be NULL.
 *      value           Pointer to memory to hold the binary value.  Shall not
 *                      be NULL.
 *      typeStruct      Pointer to type-specific functions.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is not NULL.
 *      ENOENT          No value found for "path".  "log_add()" called.
 *      EINVAL          The path name isn't valid.  "log_add()" called.
 *      EILSEQ          The value found isn't the expected type.  "log_add()"
 *                      called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus getValue(
    const char* const           path,
    void* const                 value,
    const TypeStruct* const     typeStruct)
{
    RegStatus   status = initRegistry(0);

    if (0 == status) {
        RegNode*    lastNode;
        char*       remPath;

        status = rn_getLastNode(_rootNode, path+1, &lastNode, &remPath);

        if (0 == status) {
            if (0 == *remPath) {
                log_add("\"%s\" is a node; not a value", path);
                status = ENOENT;
            }
            else if (0 == (status = flush(lastNode))) {
                if (0 == (status = sync(lastNode))) {
                    status = getNodeValue(lastNode, remPath, value, typeStruct);
                }
            }

            free(remPath);
        }                               /* "remPath" allocated */
    }

    if (0 != status && ENOENT != status)
        log_add("Couldn't get value of key \"%s\"", path);

    return status;
}

/*
 * Puts a value into a node.
 *
 * Arguments:
 *      node            Pointer to the node into which to put the value.  Shall
 *                      not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 *      typeStruct      Pointer to type-specific functions.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
static RegStatus putNodeValue(
    RegNode* const              node,
    const char* const           name,
    const void* const           value,
    const TypeStruct* const     typeStruct)
{
    RegStatus   status = initRegistry(1);

    if (0 == status) {
        if (0 == (status = typeStruct->format(value, _formatBuf))) {
            ValueThing* vt;

            if (0 == (status = rn_putValue(node, name,
                    sb_string(_formatBuf), &vt))) {
                (void)vt_setStatus(vt, NOT_SYNCHED);
            }
        }
    }

    if (status)
        log_add("Couldn't put value \"%s\" in node \"%s\"", name,
            rn_getAbsPath(node));

    return status;
}

/*
 * Puts the string representation of a value into the registry.  Makes the
 * change persistent.
 *
 * Arguments:
 *      path            Pointer to the absolute path name under which to store
 *                      the value.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 *      typeStruct      Pointer to type-specific functions.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EINVAL          The absolute path name is invalid.  "log_add()"
 *                      called. The registry is unchanged.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 *      EEXIST          A node or value would have to be created with the same
 *                      absolute path name as an existing value or node.
 *                      "log_add()" called.
 */
static RegStatus putValue(
    const char* const           path,
    const void* const           value,
    const TypeStruct* const     typeStruct)
{
    RegStatus   status = initRegistry(1);

    if (0 == status) {
        char*       nodePath;
        char*       valueName;

        if (0 == (status = reg_splitAbsPath(path, REG_SEP, &nodePath,
                &valueName))) {
            RegNode*    node;

            if (0 == (status = rn_ensure(_rootNode, nodePath, &node))) {
                if (0 == (status = putNodeValue(node, valueName, value,
                        typeStruct))) {
                    if (0 != (status = flush(node))) {
                        rn_deleteValue(node, valueName);
                    }
                }
            }

            free(valueName);
            free(nodePath);
        }                               /* "nodePath", "valueName" allocated */
    }

    return status;
}

/******************************************************************************
 * Public Functions:
 ******************************************************************************/

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
    const char* const   path)
{
    RegStatus   status;

    if (NULL != _backend) {
        log_add("Can't set registry directory to "
                "\"%s\"; registry already open in \"%s\"", path,
                getRegistryDir());
        status = EPERM;
    }
    else {
        status = setRegistryDir(path);
    }

    return status;
}

/*
 * Closes the registry.  Frees all resources and unconditionally resets the
 * module (excluding the pathname of the registry).
 *
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus reg_close(void)
{
    RegStatus   status = closeRegistry();

    resetRegistry();

    return status;
}

/*
 * Resets the registry if it exists.  Unconditionally resets this module.
 * Doesn't return the pathname of the database to its default value.
 *
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_reset(void)
{
    RegStatus   status;

    closeRegistry();

    status = beReset(getRegistryDir());

    resetRegistry();

    return status;
}

/*
 * Removes the registry if it exists.  Unconditionally resets this module.
 *
 * Returns:
 *      0               Success
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus reg_remove(void)
{
    RegStatus   status = initRegistry(1);

    if (0 == status) {
        closeRegistry();

        if (0 == status) {
            status = beRemove(getRegistryDir());
        }
    }

    resetRegistry();

    return status;
}

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
    char** const        value)
{
    return getValue(path, value, &stringStruct);
}

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
    unsigned* const     value)
{
    return getValue(path, value, &boolStruct);
}

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
    unsigned* const     value)
{
    return getValue(path, value,  &uintStruct);
}

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
    timestampt* const           value)
{
    return getValue(path, value, &timeStruct);
}

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
    signaturet* const           value)
{
    return getValue(path, value, &signatureStruct);
}

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
    const int           value)
{
    log_debug("Putting boolean %s into \"%s\"", value ? "TRUE" : "FALSE", path);

    return putValue(path, &value, &boolStruct);
}

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
    const unsigned      value)
{
    log_debug("Putting unsigned int %lu into \"%s\"", value, path);

    return putValue(path, &value, &uintStruct);
}

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
    const char* const   value)
{
    RegStatus   status;

    log_debug("Putting string \"%s\" into \"%s\"", value, path);

    status = putValue(path, value, &stringStruct);

    if (status) {
        log_add("Couldn't store value \"%s\" in parameter \"%s\"",
                value, path);
    }

    return status;
}

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
    const timestampt* const     value)
{
    static char buf[80];

    (void)sprint_timestampt(buf, sizeof(buf), value);
    log_debug("Putting time %s into \"%s\"", value, path);

    return putValue(path, value, &timeStruct);
}

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
    const signaturet    value)
{
    log_debug("Putting signature %s into \"%s\"", s_signaturet(NULL, 0, value),
            path);

    return putValue(path, value, &signatureStruct);
}

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
    const char* const   path)
{
    RegStatus   status = initRegistry(1);

    if (0 == status) {
        char*   nodePath;
        char*   valueName;

        if (0 == (status = (reg_splitAbsPath(path, REG_SEP, &nodePath,
                &valueName)))) {
            RegNode*    node;

            if (ENOENT == (status = rn_find(_rootNode, nodePath,
                    &node))) {
                status = ENOENT;
            }
            else if (0 == status) {
                if (0 == (status = rn_deleteValue(node, valueName)))
                    status = flush(node);
            }

            free(valueName);
            free(nodePath);
        }                               /* "nodePath", "valueName" allocated */
    }

    if (status && ENOENT != status)
        log_add("Couldn't delete value \"%s\"", path);

    return status;
}

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
    const int           create)
{
    RegStatus   status = reg_vetAbsPath(path);

    if (0 == status) {
        if (0 == (status = initRegistry(create))) {
            if (create) {
                status = rn_ensure(_rootNode, path+1, node);
            }
            else {
                RegNode*        lastNode;
                char*           remPath;

                if (0 == (status = rn_getLastNode(_rootNode, path+1, 
                        &lastNode, &remPath))) {
                    if (0 == *remPath) {
                        *node = lastNode;
                    }
                    else {
                        log_add("Node \"%s\" not found", path);
                        status = ENOENT;
                    }

                    free(remPath);
                }                       /* "remPath" allocated */
            }
        }
    }

    return status;
}

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
    RegNode*    node)
{
    rn_delete(node);
}

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
    RegNode*    node)
{
    RegStatus   status = initRegistry(1);

    return (0 != status)
        ? status
        : flush(node);
}

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
    const RegNode* const        node)
{
    return rn_getName(node);
}

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
    const RegNode* const        node)
{
    return rn_getAbsPath(node);
}

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
    const char*         value)
{
    return putNodeValue(node, name, value, &stringStruct);
}

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
    int                 value)
{
    return putNodeValue(node, name, &value, &boolStruct);
}

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
    unsigned            value)
{
    return putNodeValue(node, name, &value, &uintStruct);
}

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
    const timestampt*   value)
{
    return putNodeValue(node, name, value, &timeStruct);
}

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
    const signaturet    value)
{
    return putNodeValue(node, name, value, &signatureStruct);
}

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
    char** const                value)
{
    return getNodeValue(node, name, value, &stringStruct);
}

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
    int* const                  value)
{
    return getNodeValue(node, name, value, &boolStruct);
}

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
    unsigned* const             value)
{
    return getNodeValue(node, name, value, &uintStruct);
}

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
    timestampt* const           value)
{
    return getNodeValue(node, name, value, &timeStruct);
}

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
    signaturet* const           value)
{
    return getNodeValue(node, name, value, &signatureStruct);
}

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
    const char* const   name)
{
    RegStatus   status = initRegistry(1);

    if (0 == status)
        status = rn_deleteValue(node, name);

    return status;
}

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
    const NodeFunc      func)
{
    return rn_visitNodes(node, func);
}

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
    const ValueFunc     func)
{
    return rn_visitValues(node, func, NULL);
}
