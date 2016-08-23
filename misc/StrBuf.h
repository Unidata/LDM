/*
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 */

#ifndef STR_BUF_H
#define STR_BUF_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque type for this module.
 */
typedef struct strBuf StrBuf;

/**
 * Ensures that a string-buffer can hold a given number of characters.
 *
 * Arguments
 *      buf             Pointer to the string-buffer or NULL.
 *      n               The number of characters the string-buffer must hold
 *                      (excluding the terminating NUL).
 * Returns
 *      NULL            Out-of-memory or "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbEnsure(
    StrBuf*             buf,
    const size_t        n);

/**
 * Returns a new string-buffer. The buffer's string will be the empty string.
 *
 * Returns
 *      NULL            Out-of-memory.
 *      else            Pointer to the string-buffer.
 */      
StrBuf*
sbNew(void);

/**
 * Trims the string of a string-buffer by removing `isspace()` characters from
 * the end.
 *
 * @param[in] buf  The string buffer.
 * @return         The string buffer.
 */
StrBuf*
sbTrim(
        StrBuf* const buf);

/**
 * Truncates the string of a string-buffer to a given length. If the length is
 * larger than then current string, then nothing happens.
 *
 * Arguments
 *      buf             Pointer to the string-buffer to be trimmed or NULL.
 *      n               The number of characters to retain (excluding the
 *                      terminating NUL).
 * Returns
 *      NULL            "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbTruncate(
    StrBuf* const       buf,
    const size_t        n);

/**
 * Appends a variadic list of strings to the string-buffer.
 *
 * Arguments
 *      buf             Pointer to the string-buffer to append to or NULL.
 *      strings         Pointers to the strings to be appended. The last
 *                      pointer must be NULL.
 * Returns
 *      NULL            Out-of-memory or "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbCatV(
    StrBuf* const       buf,
    va_list             strings);

/**
 * Appends a list of strings to the string-buffer.
 *
 * Arguments
 *      buf             Pointer to the string-buffer to append to or NULL.
 *      ...             Pointers to the strings to be appended. The last
 *                      pointer must be NULL.
 * Returns
 *      NULL            Out-of-memory or "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbCatL(
    StrBuf* const       buf,
    ...);

/**
 * Appends a string to a string-buffer.
 *
 * Arguments
 *      buf             Pointer to the string-buffer to append to or NULL.
 *      string          The string to append.
 * Returns
 *      NULL            Out-of-memory or "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbCat(
    StrBuf* const restrict     buf,
    const char* const restrict string);

/**
 * Appends at most N characters of a string to the string-buffer.  Characters
 * past the NUL character of the string are not appended.
 *
 * Arguments
 *      buf             Pointer to the string-buffer to append to or NULL.
 *      string          The string to append.
 *      n               The number of characters to append.
 * Returns
 *      NULL            Out-of-memory or "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbCatN(
    StrBuf* const restrict buf,
    const char* restrict   string,
    const size_t           n);

/**
 * Formats a stdarg argument list.
 *
 * @param[in] buf   The string-buffer into which to format the argument list
 *                  according to the given format.
 * @param[in] fmt   The format to use.
 * @param[in] args  The stdarg argument list.
 * @retval    NULL  Out-of-memory or "buf" is NULL.
 * @return          `buf`.
 */
StrBuf*
sbPrintV(
    StrBuf* restrict           buf,
    const char* const restrict fmt,
    va_list                    args);

/**
 * Formats arguments.
 *
 * @param[in] buf   The string-buffer into which to format the arguments
 *                  according to the given format.
 * @param[in] fmt   The format to use.
 * @param[in] ...   The arguments to format.
 * @retval    NULL  Out-of-memory or "buf" is NULL.
 * @return          `buf`.
 */
StrBuf*
sbPrint(
    StrBuf* const restrict     buf,
    const char* const restrict fmt,
    ...);

/**
 * Clears a string-buffer. The string of a cleared string-buffer is the
 * empty string.
 *
 * Arguments
 *      buf             Pointer to the string-buffer to be cleared or NULL.
 * Returns
 *      NULL            "buf" is NULL.
 *      else            Pointer to the string-buffer.
 */
StrBuf*
sbClear(
    StrBuf* const       buf);

/**
 * Returns the string of a string-buffer.
 *
 * Arguments
 *      buf             Pointer to the string-buffer or NULL.
 * Returns
 *      NULL            "buf" is NULL.
 *      else            Pointer to the string of the string-buffer.
 */
const char*
sbString(
    const StrBuf* const buf);

/**
 * Frees a string-buffer.
 *
 * Arguments
 *      buf             Pointer to the string-buffer or NULL.
 */
void
sbFree(
    StrBuf* const       buf);

#ifdef __cplusplus
}
#endif

#endif
