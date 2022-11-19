/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This file implements the API for string buffers in the context of the
 *   registry.
 *
 *   The functions in this file are thread-compatible but not thread-safe.
 */
#include <config.h>

#undef NDEBUG
#include <log.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <log.h>
#include "misc.h"
#include "registry.h"
#include "string_buf.h"

struct stringBuf {
    char*       buf;    /* NUL-terminated string */
    size_t      len;    /* length of string (excluding terminating NUL) */
    size_t      max;    /* size of buffer (including terminating NUL) */
};

/*
 * Clears a string-buffer.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.  Shall not be NULL.
 *                      The internal buffer shall not be NULL.
 */
static void clear(
    StringBuf* const  strBuf)
{
    log_assert(NULL != strBuf);
    log_assert(NULL != strBuf->buf);

    strBuf->buf[0] = 0;
    strBuf->len = 0;
}

/*
 * Ensures that a string-buffer can contain a given number of characters.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.  Shall not be NULL.
 *      nbytes          The number of bytes that the string-buffer must
 *                      contain (excluding the terminating NUL)
 * Returns:
 *      0               Success
 *      ENOMEM   System error.  "log_add()" called.
 */
static RegStatus ensure(
    StringBuf* const    strBuf,
    const size_t        nbytes)
{
    RegStatus   status;
    size_t      max = nbytes + 1;

    log_assert(NULL != strBuf);

    if (max <= strBuf->max) {
        status = 0;
    }
    else {
        char*   buf = (char*)realloc(strBuf->buf, max);

        if (NULL == buf) {
            log_syserr("Couldn't allocate %lu-bytes", (unsigned long)max);
            status = ENOMEM;
        }
        else {
            strBuf->buf = buf;
            strBuf->max = max;
            status = 0;
        }
    }

    return status;
}

/*
 * Appends a string to a string-buffer.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.  Shall not be NULL.
 *      string          Pointer to the string to be appended.  Shall not be
 *                      NULL.
 * Returns:
 *      0               Success
 *      ENOMEM   System error.  "log_add()" called.
 */
static RegStatus appendString(
    StringBuf* const    strBuf,
    const char* const   string)
{
    RegStatus   status;
    size_t      newLen;

    log_assert(NULL != strBuf);
    log_assert(NULL != string);

    newLen = strBuf->len + strlen(string);

    if (0 == (status = ensure(strBuf, newLen))) {
        (void)strcpy(strBuf->buf + strBuf->len, string);

        strBuf->len = newLen;
        status = 0;
    }

    return status;
}

/*
 * Appends argument strings to a string-buffer.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer to append the arguments to.
 *                      Shall not be NULL.
 *      ap              The arguments to be appended.  The last argument shall
 *                      be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM   System error.  "log_add()" called.
 */
static RegStatus appendArgs(
    StringBuf* const    strBuf,
    va_list             ap)
{
    RegStatus   status = 0;             /* success */
    const char* string;

    for (string = (const char*)va_arg(ap, const char*); NULL != string;
            string = (const char*)va_arg(ap, const char*)) {
        if (0 != (status = appendString(strBuf, string)))
            break;
    }

    return status;
}


/******************************************************************************
 * Public API:
 ******************************************************************************/

/*
 * Returns a new instance of a string-buffer.
 *
 * Arguments:
 *      buf             Pointer to a pointer to the new buffer.  Shall not be
 *                      NULL.  Set upon successful return.  The client should
 *                      call "sb_free()" when the string-buffer is no longer
 *                      needed.
 *      nchar           Initial maximum number of characters.
 * Returns:
 *      0               Success.  "*buf" is set.
 *      ENOMEM   System error.  "log_add()" called.
 */
RegStatus sb_new(
    StringBuf** const   buf,
    const size_t        nchar)
{
    RegStatus   status;
    size_t      nbytes = sizeof(StringBuf);
    StringBuf*  instance = (StringBuf*)reg_malloc(nbytes, &status);

    if (0 == status) {
        instance->buf = NULL;
        instance->max = 0;

        if ((status = ensure(instance, nchar))) {
            free(instance);
        }
        else {
            clear(instance);
            *buf = instance;
        }
    }                                   /* "instance" allocated */

    if (status)
        log_add("Couldn't create new string-buffer");

    return status;
}

/*
 * Frees a string-buffer.
 *
 * Arguments:
 *      strBuf          Pointer to a string-buffer.  May be NULL.  Upon return,
 *                      the client shall not dereference "strBuf".
 */
void sb_free(
    StringBuf* const    strBuf)
{
    if (NULL != strBuf) {
        free(strBuf->buf);
        strBuf->buf = NULL;
        free(strBuf);
    }
}

/*
 * Ensures that a string-buffer can contain a given number of bytes.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.  Shall not be NULL.
 *      len             The number of bytes that the string-buffer must contain
 *                      (excluding the terminating NUL)
 * Returns:
 *      0               Success
 *      ENOMEM   System error.  "log_add()" called.
 */
RegStatus sb_ensure(
    StringBuf* const    strBuf,
    const size_t        len)
{
    return ensure(strBuf, len);
}

/*
 * Sets a string-buffer to the concatenation of a sequence of strings.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer to be set.  Shall not be
 *                      NULL.
 *      ...             Pointers to strings to be concatenated, in order, to
 *                      the string-buffer.  None shall be NULL except the last
 *                      pointer which shall be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus sb_set(
    StringBuf* const    strBuf,
    ...)
{
    RegStatus   status;
    va_list     ap;

    clear(strBuf);

    va_start(ap, strBuf);
    status = appendArgs(strBuf, ap);
    va_end(ap);

    return status;
}

/*
 * Sets a string-buffer to the first "n" bytes of a string.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer to be set.  Shall not be
 *                      NULL.
 *      string          Pointer to the string with which to set the buffer.
 *                      Shall not be NULL.
 *      nbytes          Number of bytes of the string to use (excluding
 *                      terminating NUL).
 * Returns:
 *      0               Success
 *      ENOMEM   System error.  "log_add()" called.
 */
RegStatus sb_nset(
    StringBuf* const    strBuf,
    const char* const   string,
    size_t              nbytes)
{
    RegStatus           status;
    const size_t        len = strlen(string);

    if (len < nbytes)
        nbytes = len;

    if (0 == (status = ensure(strBuf, nbytes))) {
        strncpy(strBuf->buf, string, nbytes);

        strBuf->buf[nbytes] = 0;
        strBuf->len = nbytes;
    }

    return status;
}

/*
 * Appends strings to a string-buffer.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer to be appended to.  Shall
 *                      not be NULL.
 *      ...             Pointers to strings to be appended, in order, to
 *                      the string-buffer.  None shall be NULL except the last
 *                      pointer which shall be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM   System error.  "log_add()" called.
 */
RegStatus sb_cat(
    StringBuf* const    strBuf,
    ...)
{
    RegStatus   status;
    va_list     ap;

    log_assert(NULL != strBuf);

    va_start(ap, strBuf);

    status = appendArgs(strBuf, ap);

    va_end(ap);

    return status;
}

/*
 * Trims a string-buffer to a given length.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.
 *      len             The maximum number of bytes to retain.  If greater than
 *                      or equal to the length of the string, then nothing
 *                      happens; otherwise, the string is truncated to the
 *                      given number of bytes.
 */
void sb_trim(
    StringBuf* const    strBuf,
    const size_t        len)
{
    if (len < strBuf->len) {
        strBuf->buf[len] = 0;
        strBuf->len = len;
    }
}

/*
 * Returns the string of a string-buffer.
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.  Shall not be NULL.
 * Returns:
 *      Pointer to the NUL-terminated string.  The client shall not modify or
 *      free.
 */
const char* sb_string(
    const StringBuf* const      strBuf)
{
    log_assert(NULL != strBuf);

    return strBuf->buf;
}

/*
 * Returns the length of the string of a string-buffer (excluding the 
 * terminating NUL).
 *
 * Arguments:
 *      strBuf          Pointer to the string-buffer.  Shall not be NULL.
 * Returns:
 *      The number of bytes in the string (excluding the terminating NUL)
 */
size_t sb_len(
    const StringBuf* const      strBuf)
{
    log_assert(NULL != strBuf);

    return strBuf->len;
}
