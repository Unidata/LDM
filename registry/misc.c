/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This file implements general-use functions for the registry.
 *
 *   The functions in this file are thread-compatible but not thread-safe.
 */
#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "registry.h"

/*
 * Allocates memory.
 *
 * Arguments:
 *      nbytes          The number of bytes to allocate
 *      status          Pointer to the status variable to be set.  Shall not be
 *                      NULL.  Upon successful return, "*status" will be 0;
 *                      otherwise it will be ENOMEM.
 * Returns:
 *      NULL            System error.  "log_start()" called.  "*status" is
 *                      ENOMEM.
 *      else            A pointer to the allocated memory.  "*status" is 0.
 */
void* reg_malloc(
    const size_t        nbytes,
    RegStatus* const    status)
{
    void*       ptr = malloc(nbytes);

    if (NULL == ptr) {
        log_serror("Couldn't allocate %lu bytes", nbytes);
        *status = ENOMEM;
    }
    else {
        *status = 0;
    }

    return ptr;
}

/*
 * Clones the prefix of a string.  Logs a message if an error occurs.
 *
 * ARGUMENTS:
 *      clone           Pointer to a pointer to the clone.  Set upon successful
 *                      return.  Shall not be NULL.  The client should call
 *                      "free(*clone)" when the clone is no longer needed.
 *      string          Pointer to the string to clone.  Shall not be NULL.
 *      nbytes          The number of bytes of prefix to clone
 * RETURNS:
 *      0               Success.  "*clone" is not NULL.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus reg_clonePrefix(
    char** const        clone,
    const char* const   string,
    size_t              nbytes)
{
    RegStatus   status;
    char*       copy = (char*)reg_malloc(nbytes+1, &status);

    if (NULL != copy) {
        strncpy(copy, string, nbytes)[nbytes] = 0;
        *clone = copy;
        status = 0;
    }
    else {
        log_serror("Couldn't clone first %lu bytes of string \"%s\"",
            nbytes, string);
        status = ENOMEM;
    }

    return status;
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
 *      0               Success.  "*clone" is not NULL.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus reg_cloneString(
    char** const        clone,
    const char* const   src)
{
    return reg_clonePrefix(clone, src, strlen(src));
}

/*
 * Indicates if a path is absolute or not.
 *
 * Arguments:
 *      path            The path to be checked.  Shall not be NULL.
 * Returns:
 *      0               The path is not absolute
 *      else            The path is absolute
 */
int reg_isAbsPath(const char* const path)
{
    return REG_SEP[0] == path[0];
}

/*
 * Indicates if a path is the absolute path name of the root node.
 *
 * Arguments:
 *      path            The path to be checked.  Shall not be NULL.
 * Returns:
 *      0               The path is not the absolute pathname of the root node
 *      else            The path is the absolute pathname of the root node
 */
int reg_isAbsRootPath(const char* const path)
{
    return reg_isAbsPath(path) && 0 == path[1];
}

/*
 * Ensures that a path name is absolute.
 *
 * Arguments:
 *      path            The path name to be vetted.  Shall not be NULL.
 * Returns:
 *      0               The path name is absolute
 *      EINVAL     The path name isn't absolute.  "log_start()" called.
 */
RegStatus reg_vetAbsPath(
    const char* const   path)
{
    RegStatus   status;

    if (reg_isAbsPath(path)) {
        status = 0;
    }
    else {
        log_start("Not an absolute path name: \"%s\"", path);
        status = EINVAL;
    }

    return status;
}

/*
 * Returns the parent pathname of a child pathname.  The child pathname may
 * be absolute or relative.
 *
 * Arguments:
 *      child           Pointer to the child pathname whose parent pathname is
 *                      to be returned.  Shall not be NULL.  May point to an
 *                      empty string, in which case ENOENT will be
 *                      returned.
 *      parent          Pointer to a pointer to the parent pathname.  Shall not
 *                      be NULL.  Set upon successful return.  The client
 *                      should call "free(*parent)" when the parent pathname is
 *                      no longer needed.
 * Returns:
 *      0               Success.  "*parent" is not NULL.
 *      ENOENT          The child pathname has no parent pathname
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus reg_getParentPath(
    const char* const   child,
    char** const        parent)
{
    RegStatus   status;
    char*       lastSep = strrchr(child, REG_SEP[0]);

    if (NULL == lastSep) {
        status = (0 == child[0])
            ? ENOENT
            : reg_cloneString(parent, "");
    }
    else {
        if (lastSep == child) {
            status = (0 == child[1])
                ? ENOENT
                : reg_cloneString(parent, REG_SEP);
        }
        else {
            status = reg_clonePrefix(parent, child, lastSep - child);
        }
    }

    return status;
}

/*
 * Splits an absolute path name into relative path name and value-name
 * components.
 *
 * Arguments:
 *      path            Pointer to the absolute path name.  Shall not be
 *                      NULL.  Client may free upon return.
 *      absPath         Pointer to the absolute path name on which to base the
 *                      returned relative pathname.  Shall not be NULL.
 *      relPath         Pointer to a pointer to the path name of "path"
 *                      relative to "absPath" without the value-name component.
 *                      Shall not be NULL.  Set upon successful return.  The
 *                      client should call "free(*relPath)" when the path is no
 *                      longer needed.
 *      valueName       Pointer to a pointer to the name of the corresponding
 *                      value.  Shall not be NULL.  Set upon successful return.
 *                      The client should call "free(*valueName)" when the name
 *                      is no longer needed.
 * Returns:
 *      0               Success.  "*relPath" and "*valueName" are set.
 *      EINVAL          "path" isn't valid.  "log_start()" called.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus reg_splitAbsPath(
    const char* const   path,
    const char* const   absPath,
    char** const        relPath,
    char** const        valueName)
{
    RegStatus   status = reg_vetAbsPath(path);

    if (0 == status) {
        if (0 == (status = reg_vetAbsPath(absPath))) {
            if (strstr(path, absPath) != path) {
                log_start("Path \"%s\" doesn't have prefix \"%s\"", path,
                    absPath);
                status = EINVAL;
            }
            else {
                const char*     lastSep = strrchr(path, REG_SEP[0]);

                if (NULL == lastSep) {
                    log_start("Not a valid path to a value: \"%s\"", path);
                    status = EINVAL;
                }
                else {
                    char*       name;

                    if (0 == (status = reg_cloneString(&name, lastSep+1))) {
                        const char*     relStart = path + strlen(absPath);
                        size_t          nbytes;

                        if (REG_SEP[0] == *relStart)
                            relStart++;

                        nbytes = (lastSep < relStart)
                            ? 0
                            : lastSep - relStart;

                        if (0 == (status = reg_clonePrefix(relPath, relStart,
                                nbytes))) {
                            *valueName = name;
                        }               /* "*relPath" allocated */

                        if (status)
                            free(name);
                    }                   /* "name" allocated */
                }
            }
        }
    }

    return status;
}
