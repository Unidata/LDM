/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This file implements the API for the registry.
 *
 *   The functions in this file are thread-compatible but not thread-safe.
 */
#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include <ldmprint.h>
#include <log.h>
#include "registry.h"
#include "stringBuf.h"
#include <timestamp.h>

struct regNode {
};

struct regCursor {
};

/*
 * Parses a string into a value.
 *
 * Arguments:
 *      string          Pointer to the string to be "parsed".  Shall not be
 *                      NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
typedef RegStatus (*Parser)(const char* string, void* value);
/*
 * Sets a value to a default value.
 *
 * Arguments:
 *      dest            Pointer to the value to be set.  Shall not be NULL.
 *      src             Pointer to the default value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*dest" is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
typedef RegStatus (*Setter)(void* value, const void* defaultValue);
/*
 * Formats a value into a string.
 *
 * Arguments:
 *      value           Pointer to the value to be formatted.  Shall not be
 *                      NULL.
 *      strBuf          Pointer to a string-buffer to receive the formatted
 *                      representation of the value.  Shall not be NULL.
 * Returns:
 *      0               Success.  "*strBuf" is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
typedef RegStatus (*Formatter)(const void* value, StringBuf* strBuf);

typedef enum {
    /* Start with 0 to enable use as an index: */
    REG_STRING = 0,
    REG_UINT,
    REG_TIME,
    REG_SIGNATURE,
    /* Keep at end to track the number of types: */
    REG_TYPECOUNT
}       RegType;

typedef struct {
    Parser      parse;
    Setter      setDefault;
    Formatter   format;
}       TypeStruct;

static int                      _initialized;   /* Module is initialized? */
static Backend*                 _backend;       /* backend database */
static int                      _forWriting;    /* registry open for writing? */
static StringBuf*               _pathBuf;       /* buffer for creating paths */
static StringBuf                _formatBuf;     /* values-formatting buffer */
static const TypeStruct*        _typeStructs[REG_TYPECOUNT];

/******************************************************************************
 * Private Functions:
 ******************************************************************************/

/*
 * Indicates if a path is absolute or not.
 *
 * Arguments:
 *      path            The path to be checked.  Shall not be NULL.
 * Returns:
 *      0               The path is not absolute
 *      else            The path is absolute
 */
static int isAbsolutePath(const char* const path)
{
    assert(NULL != path);

    return REG_SEP[0] == path[0];
}

/*
 * Clones a string.  Logs a message if an error occurs.
 *
 * ARGUMENTS:
 *      clone           Pointer to a pointer to the clone.  Set upon successful
 *                      return.  Shall not be NULL.  The client should call
 *                      "free(*clone)" when the clone is no longer needed.
 *      src             The string to clone.  Shall not be NULL.
 * RETURNS:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus cloneString(
    char** const        clone,
    const char* const   src)
{
    RegStatus   status;
    char*       copy;

    assert(NULL != clone);
    assert(NULL != src);

    copy = strdup(src);

    if (NULL != copy) {
        *clone = copy;
        status = 0;
    }
    else {
        log_serror("Couldn't clone %lu-byte string \"%s\"",
            (unsigned long)strlen(src), src);
        status = REG_SYS_ERROR;
    }

    return status;
}

/*
 * Returns the "../default" form of an absolute path name.
 *
 * Arguments:
 *      path           Pointer to the path name to have its "default" path
 *                     name returned.  Shall not be NULL.
 *      strBuf         Pointer to the string-buffer to receive the default
 *                     path.  Shall not be NULL.
 * Returns:
 *      0              Success
 *      REG_BAD_ARG    A default path name can't be constructed from "path".
 *                     "log_start()" called.
 *      REG_SYS_ERROR  System error.  "log_start()" called.
 */
static RegStatus makeDefaultPath(
    const char* const  path,
    StringBuf* const   strBuf)
{
   RegStatus           status = 0;     /* success */
   char*               lastSep;

   assert(NULL != path);

   lastSep = strrchr(path, REG_SEP[0]);

   if (NULL == lastSep || lastSep == path) {
       status = REG_BAD_ARG;
   }
   else if (0 == (status = sb_set(strBuf, path, NULL))) {
       if (0 == (status = sb_trim((size_t)(lastSep - path))))
           status = sb_cat(strBuf, REG_SEP, REG_DEFAULT, NULL);
   }

   if (status)
       log_start("Can't construct default path for \"%s\"", path);

   return status;
}

/*
 * Closes the registry if it's open.
 */
static void closeRegistry(void)
{
    if (NULL != _backend) {
        (void)beClose(_backend);
        _backend = NULL;
    }
    if (NULL != _pathBuf) {
        sb_free(_pathBuf);
        _pathBuf = NULL;
    }
    if (NULL != _formatBuf) {
        sb_free(_formatBuf);
        _formatBuf = NULL;
    }
}

/*
 * "Parses" a string into a value.
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
    return cloneString((char**)value, string);
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
 *      0               Success.  The string-buffer is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus formatString(
    const void* const   value,
    StringBuf* const    strBuf)
{
    return sb_set(strBuf, (const char*)value, NULL);
}

/*
 * Assigns a default string to a value.
 *
 * Arguments:
 *      dest            Pointer to the value to be set.  Shall not be NULL.
 *                      The client should call "free(*dest)" when the value
 *                      is no longer needed.
 *      src             Pointer to the value to be assigned
 * Returns:
 *      0               Success.  "*dest" is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus setString(
    void* const         dest,
    const void* const   src)
{
    return cloneString((char**)dest, (const char*)src);
}

/*
 * Parses a string into an unsigned integer.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      REG_WRONG_TYPE  The string doesn't represent an unsigned integer
 */
static RegStatus parseUint(
    const char* const   string,
    void* const         value)
{
    RegStatus           status;
    char*               end;
    unsigned long       val;

    assert(NULL != string);
    assert(NULL != value);

    errno = 0;
    val = strtoul(string, &end, 0);

    if (0 != *end || (0 == val && 0 != errno)) {
        status = REG_WRONG_TYPE;
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
 *      0               Success.  The string-buffer is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus formatUint(
    const void* const   value,
    StringBuf* const    strBuf)
{
    static char buf[80];

    (void)snprintf(buf, sizeof(buf)-1, "%u", (unsigned*)value);

    return sb_set(strBuf, buf, NULL);
}

/*
 * Assigns a default unsigned integer to a value.
 *
 * Arguments:
 *      dest            Pointer to the value to be set.
 *      src             Pointer to the value to be assigned.
 */
static RegStatus setUint(
    void* const         dest,
    const void* const   src)
{
    *(unsigned*)dest = *(unsigned*)src;

    return 0;
}

/*
 * Parses a string into a time.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      REG_WRONG_TYPE  The string doesn't represent a time
 */
static RegStatus parseTime(
    const char* const   string,
    void* const         value)
{
    RegStatus   status;
    timestampt  val;

    assert(NULL != string);
    assert(NULL != value);

    status = tsParse(string, &val);

    if (0 > status || 0 != string[status]) {
        status = REG_WRONG_TYPE;
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
 *      0               Success.  The string-buffer is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus formatTime(
    const void* const   value,
    StringBuf* const    strBuf)
{
    return sb_set(strBuf, tsFormat((const timestampt*)value, NULL));
}

/*
 * Assigns a default time to a value.
 *
 * Arguments:
 *      dest            Pointer to the value to be set
 *      src             Pointer to the value to be assigned
 */
static RegStatus setTime(
    void* const         dest,
    const void* const   src)
{
    *(timestampt*)dest = *(timestampt*)src;

    return 0;
}

/*
 * Parses a string into a signature.
 *
 * Arguments:
 *      string          Pointer to the string to be parsed.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      REG_WRONG_TYPE  The string doesn't represent a signature
 */
static RegStatus parseSignature(
    const char* const   string,
    void* const         value)
{
    RegStatus   status;
    signaturet  val;

    assert(NULL != string);
    assert(NULL != value);

    status = sigParse(string, &val);

    if (0 > status || 0 != string[status]) {
        status = REG_WRONG_TYPE;
    }
    else {
        (void)memcpy((signaturet*)value, val, sizeof(signaturet));
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
 *      0               Success.  The string-buffer is set.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus formatSignature(
    const void* const   value,
    StringBuf* const    strBuf)
{
    return sb_set(strBuf, s_signaturet(NULL, 0, (const signaturet)value), NULL);
}

/*
 * Assigns a default signature to a value.
 *
 * Arguments:
 *      dest            Pointer to the value to be set
 *      src             Pointer to the value to be assigned
 */
static RegStatus setSignature(
    void* const         dest,
    const void* const   src)
{
    (void)memcpy((signaturet*)dest, src, sizeof(signaturet));

    return 0;
}

static const TypeStruct  stringStruct = {parseString, setString, formatString};
static const TypeStruct  uintStruct = {parseUint, setUint, formatUint};
static const TypeStruct  timeStruct = {parseTime, setTime, formatTime};
static const TypeStruct  signatureStruct =
    {parseSignature, setSignature, formatSignature};

/*
 * Initializes the registry.  Ensures that the backend is open for the desired
 * access.  Registers the process termination function.
 *
 * Arguments:
 *      forWriting      Indicates if the backend should be open for writing:
 *                      0 <=> no.
 * Returns:
 *      0               Success
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus initRegistry(const int forWriting)
{
   RegStatus   status = 0;             /* success */

   if (!_initialized) {
       if (0 != atexit(closeRegistry)) {
           log_serror("Couldn't register closeRegistry() with atexit()");
           status = REG_SYS_ERROR;
       }
       else {
           if (NULL == (_pathBuf = sb_new(80))) {
               log_add("Couldn't allocate path-buffer");
               status = REG_SYS_ERROR;
           }
           else {
               if (NULL == (_formatBuf = sb_new(80))) {
                   log_add("Couldn't allocate formating-buffer");
                   status = REG_SYS_ERROR;
               }
               else {
                   _typeStructs[REG_STRING] = &stringStruct;
                   _typeStructs[REG_UINT] = &uintStruct;
                   _typeStructs[REG_TIME] = &timeStruct;
                   _typeStructs[REG_SIGNATURE] = &signatureStruct;
                   _initialized = 1;
               }                       /* "_formatBuf" allocated */

               if (status) {
                   sb_free(_pathBuf);
                   _pathBuf = NULL;
               }
           }                           /* "_pathBuf" allocated */
       }                               /* "closeRegistry()" registered */
   }                                   /* module not initialized */

   if (0 == status) {
       if (NULL != _backend && forWriting && !_forWriting) {
           /* The backend is open for the wrong (read-only) access */
           status = beClose(_backend);
           _backend = NULL;
       }

       if (!status && NULL == _backend) {
           /* The backend isn't open. */
           if (0 == (status = beOpen(&_backend, REGISTRY_PATH, forWriting))) {
               _forWriting = forWriting;
           }                           /* "_backend" allocated and open */
       }                               /* backend database not open */
   }

   return status;
}

/*
 * Returns the binary value of a value-node from the registry.
 *
 * Preconditions:
 *      The registry has been initialized.
 * Arguments:
 *      path            Pointer to the absolute path name of the value-node
 *                      whose value will be returned.  Shall not be NULL.
 *      parse           Function for parsing the string representation of the
 *                      value into its binary representation.
 *      value           Pointer to memory to hold the binary value.  Shall not
 *                      be NULL.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      REG_NO_VALUE    No value found
 *      REG_WRONG_TYPE  The found value is the wrong type.  "log_start()"
 *                      called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus getX(
    const char*         path,
    RegStatus           parse(const char* string, void* value),
    void*               value)
{
    RegStatus   status;
    char*       string;

    assert(isAbsolutePath(path));
    assert(NULL != parse);

    status = beGet(_backend, path, &string);

    if (0 == status) {
        status = parse(string, value);

        free(string);
    }                               /* "string" allocated */

    return status;
}

/*
 * Returns the binary value of a value-node from the registry.  If the
 * value-node doesn't exist, then the value of the "default" value-node of the
 * parent map-node is returned if it exists.  If no value is found, then the
 * value of the default argument is returned.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value-node
 *                      whose value will be returned.  Shall not be NULL.
 *      value           Pointer to memory to hold the binary value.  Shall not
 *                      be NULL.
 *      typeStruct      Pointer to type-specific functions.  Shall not be NULL.
 *      defaultValue    Pointer to the default value to which "value" will be
 *                      set if no value is found.  Shall not be NULL.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      REG_NO_VALUE    No value found.  "*value" is set to the default value.
 *      REG_WRONG_TYPE  The found value isn't an unsigned integer.
 *                      "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
static RegStatus get(
    const char*         path,
    void*               value,
    const TypeStruct*   typeStruct,
    const void*         defaultValue)
{
    RegStatus   status;

    assert(NULL != typeStruct);

    if (0 == (status = initRegistry(0))) {
        if (REG_NO_VALUE == (status = getX(path, typeStruct->parse, value))) {
            const char*     defaultPath;

            if (REG_BAD_ARG == (status = makeDefaultPath(path, &_pathBuf))) {
                /* Can't make a "default" path */
                status = typeStruct->setDefault(value, defaultValue);
            }
            else if (0 == status) {
                if (REG_NO_VALUE == (status = getX(sb_string(_pathBuf),
                        typeStruct->parse, value)))
                    status = typeStruct->setDefault(value, defaultValue);
            }
        }                               /* value not found under "path" */
    }                                   /* registry opened */

    return status;
}

/*
 * Puts the string representation of a value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name under which to store
 *                      the value.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.
 *      typeStruct      Pointer to type-specific functions.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 */
static RegStatus put(
    const char* const           path,
    const void* const           value,
    const TypeStruct* const     typeStruct)
{
    RegStatus   status = initRegistry(1);

    if (0 == status) {
        if (0 == (status = typeStruct->format(value, &_formatBuf)))
            status = bePut(_backend, path, &_formatBuf);
    }

    return status;
}

/******************************************************************************
  * Public API:
  ******************************************************************************/

/*
 * Creates the registry if it doesn't already exist.
 *
 * Returns:
 *      0               Success
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_create(void)
{
    return 0;
}

/*
 * Removes the registry if it exists.  Upon successful return, attempts to
 * access the registry will fail.
 *
 * Returns:
 *      0               Success
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_remove(void)
{
    return 0;
}

/*
 * Returns the value of a value-node from the registry as a string.
 * If the value-node doesn't exist, then the value of the "default" value-node
 * of the parent map-node is returned if it exists.  If no value is found, then
 * the value of the default argument is returned.  Every value has a string
 * representation.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the node whose
 *                      value will be returned.  Shall not be NULL.
 *      value           Pointer to a pointer to be set.  Shall not be NULL.
 *                      Upon successful return, the client should call
 *                      "free(*value)" when the value is no longer needed
 *                      (regardless of whether it is set to the default value).
 *      defaultValue    The default value to return if no value is found.  May
 *                      be NULL.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      REG_NO_VALUE    No value found.  "*value" is set to the default value.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_getString(
    const char*         path,
    char** const        value,
    const char*         defaultValue)
{
    return get(path, value, &stringStruct, defaultValue);
}

/*
 * Returns the value of a value-node from the registry as an unsigned integer.
 * If the value-node doesn't exist, then the value of the "default" value-node
 * of the parent map-node is returned if it exists.  If no value is found, then
 * the value of the default argument is returned.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value-node
 *                      whose value will be returned.  Shall not be NULL.
 *      value           Pointer to memory to hold the value.  Shall not be
 *                      NULL.
 *      defaultValue    The default value to return if no value is found
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      REG_BAD_ARG     "path" isn't an absolute path name
 *      REG_NO_VALUE    No value found.  "*value" is set to the default value.
 *      REG_WRONG_TYPE  The found value isn't an unsigned integer.
 *                      "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_getUint(
    const char*         path,
    unsigned*           value,
    unsigned            defaultValue)
{
    return get(path, value, &uintStruct,  &defaultValue);
}

/*
 * Returns the value of a value-node from the registry as a time-value.
 * If the value-node doesn't exist, then the value of the "default" value-node
 * of the parent map-node is returned if it exists.  If no value is found, then
 * the value of the default argument is returned.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the node whose
 *                      value will be returned.  Shall not be NULL.
 *      value           Pointer to memory to hold the value.  Shall not be NULL.
 *                      The client may free upon return.
 *      defaultValue    Pointer to the default value to return if no value is
 *                      found.  Shall not be NULL.
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      REG_NO_VALUE    No value found.  "*value" is set to the default value.
 *      REG_WRONG_TYPE  The found value isn't a timestamp.  "log_start()"
 *                      called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_getTime(
    const char*         path,
    timestampt*         value,
    const timestampt*   defaultValue)
{
    return get(path, value, &timeStruct, defaultValue);
}

/*
 * Returns the value of a value-node from the registry as a signature.
 * If the value-node doesn't exist, then the value of the "default" value-node
 * of the parent map-node is returned if it exists.  If no value is found, then
 * the value of the default argument is returned.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the node whose
 *                      value will be returned.  Shall not be NULL.
 *      value           Pointer to memory to hold the value.  Shall not be NULL.
 *                      The client may free upon return.
 *      defaultValue    The default value to return if no value is found
 * Returns:
 *      0               Success.  Value found and put in "*value".
 *      REG_NO_VALUE    No value found.  "*value" is set to the default value.
 *      REG_WRONG_TYPE  The found value is the wrong type.  "log_start()"
 *                      called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_getSignature(
    const char*         path,
    signaturet*         value,
    const signaturet*   defaultValue)
{
    return get(path, value, &signatureStruct, defaultValue);
}

/*
 * Puts an unsigned integer value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist.
 *      value           The value to be written to the registry.
 * Returns:
 *      0               Success.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_putUint(
    const char*         path,
    unsigned            value)
{
    return 0;
}

/*
 * Puts a string value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist.
 *      value           Pointer to the value to be written to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_putString(
    const char*         path,
    const char*         value)
{
    return 0;
}

/*
 * Puts a time value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist.
 *      value           Pointer to the value to be written to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_putTime(
    const char*         path,
    const timestampt*   value)
{
    return 0;
}

/*
 * Puts a signature value into the registry.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the value.  Shall
 *                      not be NULL.  Ancestral nodes are created if they
 *                      don't exist.
 *      value           Pointer to the value to be written to the registry.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_putSignature(
    const char*         path,
    const signaturet*   value)
{
    return 0;
}

/*
 * Returns the node in the registry corresponding to an absolute path name .
 *
 * Arguments:
 *      path            The absolute path name of the node to be returned.
 *                      Shall not be NULL.  The empty string obtains the
 *                      top-level node.
 *      node            Pointer to a pointer to a node.  Set on success.  Shall
 *                      not be NULL.  The client should call
 *                      "regNode_free(*node)" when the node is no longer
 *                      needed.
 * Returns:
 *      0               Success.  "*node" is set.  The client should call
 *                      "regNode_free(*node)" when the node is no longer
 *                      needed.
 *      REG_NO_NODE     No node found
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_getNode(
    const char*         path,
    RegNode*            node)
{
    return 0;
}

/*
 * Writes a node to the registry.
 *
 * Arguments:
 *      node            Pointer to the node to be written to the registry.
 *                      Shall not be NULL.  Ancestral nodes are created if they
 *                      don't exist.  The client may call "regNode_free()"
 *                      upon return.
 * Returns:
 *      0               Success
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       reg_putNode(RegNode* node)
{
    return 0;
}

/*
 * Returns a newly-allocated node.
 *
 * Arguments:
 *      path            Pointer to the absolute path name of the node.  Shall
 *                      not be NULL.  The client may free upon return.
 * Returns:
 *      NULL            System error
 *      else            Pointer to a newly-allocated node with the given
 *                      absolute path name .  The client should call
 *                      "regNode_free()" when the node is no longer needed.
 */
RegNode*        regNode_new(const char* path)
{
    return 0;
}

/*
 * Returns the type of a node.
 *
 * Arguments:
 *      node            Pointer to the node to have its type returned.  Shall
 *                      not be NULL.
 * Returns:
 *      One of REG_MAP or REG_VALUE.
 */
RegNodeType     regNode_type(RegNode* node)
{
    return 0;
}

/*
 * Returns the name of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose name is to be returned.  Shall
 *                      not be NULL.
 * Returns:
 *      Pointer to an internal buffer containing the name of the node.  The
 *      client shall not free the buffer.
 */
const char*     regNode_name(RegNode* node)
{
    return 0;
}

/*
 * Returns the absolute path name of a node.
 *
 * Arguments:
 *      node            Pointer to the node to have its absolute path name
 *                      returned.  Shall not be NULL.
 * Returns:
 *      Pointer to an internal buffer containing the absolute path name of the
 *      node.  The client shall not free the buffer.
 */
const char*     regNode_absolutePath(RegNode* node)
{
    return 0;
}

/*
 * Frees a returned node.
 *
 * Arguments:
 *      node            Pointer to the node to be freed
 */
void            regNode_free(RegNode* node)
{
    return 0;
}

/*
 * Deletes a node.  Deleting a node also deletes its children.
 *
 * Arguments:
 *      node            Pointer to the node to be deleted
 * Returns:
 *      0               Success
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regNode_delete(RegNode* node)
{
    return 0;
}

/*
 * Returns the value of a value-node as a string.
 *
 * Arguments:
 *      node            Pointer to the value-node whose value is to be returned
 *                      as a string.  Shall not be NULL.
 *      value           Pointer to a pointer to a string.  Shall not be NULL.
 *                      Set upon successful return.  The client shall not call
 *                      "free(*value)".
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_NO_VALUE    The node is not a value-node.
 */
RegStatus       regNode_getString(
    RegNode*            node,
    const char**        value)
{
    return 0;
}

/*
 * Returns the value of a value-node as an unsigned integer.
 *
 * Arguments:
 *      node            Pointer to the value-node whose unsigned integer value
 *                      is to be returned.  Shall not be NULL.
 *      value           Pointer to an unsigned integer.  Set upon success.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_NO_VALUE    The node is not a value-node.  "log_start()" called.
 *      REG_WRONG_TYPE  The value isn't an unsigned integer.  "log_start()"
 *                      called.
 */
RegStatus       regNode_getUint(
    RegNode*            node,
    unsigned*           value)
{
    return 0;
}

/*
 * Returns the value of a value-node as a time.
 *
 * Arguments:
 *      node            Pointer to the value-node whose value is to
 *                      be returned as a time.  Shall not be NULL.
 *      value           Pointer to a time.  Set upon success.  Shall not be
 *                      NULL.  The client may free upon return.
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_NO_VALUE    The node is not a value-node.  "log_start()" called.
 *      REG_WRONG_TYPE  The value isn't a time.  "log_start()" called.
 */
RegStatus       regNode_getTime(
    RegNode*            node,
    timestampt*         value)
{
    return 0;
}

/*
 * Returns the value of a value-node as a signature.
 *
 * Arguments:
 *      node            Pointer to the value-node whose value is to
 *                      be returned as a signature.  Shall not be NULL.
 *      value           Pointer to a signature.  Set upon success.  Shall not
 *                      be NULL.  The client may free upon return.
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_NO_VALUE    The node is not a value-node.  "log_start()" called.
 *      REG_WRONG_TYPE  The value isn't a signature.  "log_start()" called.
 */
RegStatus       regNode_getSignature(
    RegNode*            node,
    signaturet*         value)
{
    return 0;
}

/*
 * Returns the value of a value-node as a string given the parent map-node and
 * the name of the value-node.
 *
 * Arguments:
 *      parent          Pointer to the parent map-node.  Shall not be NULL.
 *      name            Pointer to the name of the child-node whose value is to
 *                      be returned as a string.  Shall not be NULL.
 *      value           Pointer to a pointer to a string.  Set upon success.
 *                      Shall not be NULL.  Upon successful return, the client
 *                      shall not call "free(*value)".
 * Returns:
 *      0               Success.  "*value" is set.  The client shall not call
 *                      "free(*value)".
 *      REG_BAD_ARG     The given parent-node isn't a map-node.  "log_start()"
 *                      called.
 *      REG_NO_NODE     No such child-node exists with the given name.
 *                      "log_start()" called.
 *      REG_NO_VALUE    The child-node exists but is not a value-node.
 *                      "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_getString(
    RegNode*            parent,
    const char*         name,
    const char**        value)
{
    return 0;
}

/*
 * Returns the value of a value-node as an unsigned integer given the parent
 * map-node and the name of the value-node.
 *
 * Arguments:
 *      parent          Pointer to the parent map-node.  Shall not be NULL.
 *      name            Pointer to the name of the child-node whose value is
 *                      to be returned as an unsigned integer.  Shall not be
 *                      NULL.
 *      value           Pointer to an unsigned integer.  Set upon success.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_BAD_ARG     The given parent-node isn't a map-node.  "log_start()"
 *                      called.
 *      REG_NO_NODE     No such child-node exists with the given name.
 *                      "log_start()" called.
 *      REG_NO_VALUE    The child-node exists but isn't a value-node.
 *                      "log_start()" called.
 *      REG_WRONG_TYPE  The value isn't an unsigned integer.  "log_start()"
 *                      called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_getUint(
    RegNode*            parent,
    const char*         name,
    unsigned*           value)
{
    return 0;
}

/*
 * Returns the value of a value-node as a time given the parent map-node and
 * the name of the value-node.
 *
 * Arguments:
 *      parent          Pointer to the parent map-node.  Shall not be NULL.
 *      name            Pointer to the name of the child-node whose value
 *                      is to be returned as a time.  Shall not be NULL.
 *      value           Pointer to a time.  Set upon success.  Shall not be
 *                      NULL.  The client may free upon return.
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_BAD_ARG     The given parent-node isn't a map-node.  "log_start()"
 *                      called.
 *      REG_NO_NODE     No such child-node exists with the given name.
 *                      "log_start()" called.
 *      REG_NO_VALUE    The child-node exists but isn't a value-node.
 *                      "log_start()" called.
 *      REG_WRONG_TYPE  The value isn't a time.  "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_getTime(
    RegNode*            parent,
    const char*         name,
    timestampt*         value)
{
    return 0;
}

/*
 * Returns the value of a value-node as a signature given the parent map-node
 * and the name of the value-node.
 *
 * Arguments:
 *      parent          Pointer to the parent map-node.  Shall not be NULL.
 *      name            Pointer to the name of the child-node whose value
 *                      is to be returned as a signature.  Shall not be NULL.
 *      value           Pointer to a signature.  Set upon success.  Shall not
 *                      be NULL.  The client may free upon return.
 * Returns:
 *      0               Success.  "*value" is set.
 *      REG_BAD_ARG     The given parent-node isn't a map-node.  "log_start()"
 *                      called.
 *      REG_NO_NODE     No such child-node exists with the given name.
 *                      "log_start()" called.
 *      REG_NO_VALUE    The child-node exists but isn't a value-node.
 *                      "log_start()" called.
 *      REG_WRONG_TYPE  The value isn't a signature.  "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_getSignature(
    RegNode*            parent,
    const char*         name,
    signaturet*         value)
{
    return 0;
}

/*
 * Adds a string value-node to a map-node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return.
 *      value           Pointer to the string value.  Shall not be NULL.  The
 *                      may free upon return.
 * Returns:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_putString(
    RegNode*            node,
    const char*         name,
    const char*         value)
{
    return 0;
}

/*
 * Adds an unsigned integer value-node to a map-node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return.
 *      value           The unsigned integer value
 * Returns:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_putUint(
    RegNode*            node,
    const char*         name,
    unsigned            value)
{
    return 0;
}

/*
 * Adds a time value-node to a map-node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return.
 *      value           Pointer to the time value.  Shall not be NULL.
 *                      The client may free upon return.
 * Returns:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_putTime(
    RegNode*            node,
    const char*         name,
    const timestampt*   value)
{
    return 0;
}

/*
 * Adds a signature value-node to a map-node.
 *
 * Arguments:
 *      node            Pointer to the parent node.  Shall not be NULL.
 *      name            Pointer to the name of the value-node.  Shall not be
 *                      NULL.  The client may free upon return.
 *      value           Pointer to the signature value.  Shall not be NULL.
 *                      The client may free upon return.
 * Returns:
 *      0               Success
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regMap_putSignature(
    RegNode*            node,
    const char*         name,
    const signaturet*   value)
{
    return 0;
}

/*
 * Returns an interator over the children of a map-node.
 *
 * Arguments:
 *      node            Pointer to the node whose children will be iterated.
 *                      Shall not be NULL.
 *      cursor          Pointer to a pointer to an iterator.  Set on success.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.  "*cursor" is set.  The client should call 
 *                      "regCursor_free(*cursor)" when the iterator is no
 *                      longer needed.
 *      REG_BAD_ARG     The given node isn't iterable.  "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regNode_newCursor(
    RegNode*            node,
    RegCursor*          cursor)
{
    return 0;
}

/*
 * Returns the next child-node.  Returns the first child-node on the first
 * call.
 *
 * Arguments:
 *      cursor          Pointer to the iterator over the child-nodes of a node.
 *                      Shall not be NULL.
 *      node            Pointer to a pointer to a node.  Set upon success.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success.  "*node" is set.  The client shall not call
 *                      "regNode_free(*node)".
 *      REG_NO_NODE     No more child-nodes.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 */
RegStatus       regCursor_next(
    RegCursor*          cursor,
    RegNode**           node)
{
    return 0;
}

/*
 * Frees an iterator.
 *
 * Arguments:
 *      cursor          Pointer to the iterator to be freed.  Upon return,
 *                      the client shall not dereference "cursor".
 */
void            regCursor_free(RegCursor* cursor)
{
}
