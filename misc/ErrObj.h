/**
 * This file declares an error-object. An error-object comprises a sequence of individual errors,
 * from the first (i.e., earliest) error to the last (i.e., most recent) error.
 *
 *        File: ErrObj.h
 *  Created on: Nov 17, 2022
 *      Author: Steven R. Emmerson
 */

#ifndef MISC_ERROBJ_H_
#define MISC_ERROBJ_H_

#include <errno.h>
#include <pthread.h>
#include <string.h>

/// An error object
typedef struct ErrObj ErrObj;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a new error object.
 * @param[in] file     The name of the file in which the error occurred
 * @param[in] line     The line number in the file to associate with the error
 * @param[in] func     The name of the function in which the error occurred
 * @param[in] code     The error's code
 * @param[in] fmt      The format for the error message
 * @param[in] ...      The format's arguments
 * @retval    NULL     Failure
 * @return             A new error object
 */
ErrObj* eo_new(
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        ...);

/**
 * Deletes (i.e., frees) an error-object.
 * @param[in] errObj  The error-object to be deleted or `NULL`
 */
void eo_delete(ErrObj* errObj);

/**
 * Wraps a prevously-occuring error with a later-occuring error.
 * @param[in] errObj  The previous error-object
 * @param[in] file    The name of the file in which the error occurred
 * @param[in] line    The line number in the file to associate with the error
 * @param[in] func    The name of the function in which the error occurred
 * @param[in] code    The error's code
 * @param[in] fmt     The format for the error message or `NULL`
 * @param[in] ...     The format's arguments. Ignored if the format is `NULL`.
 * @retval    errObj  Success
 * @retval    NULL    Out-of-memory
 * @return            An error object corresponding to the arguments
 */
ErrObj* eo_wrap(
        ErrObj*     errObj,
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        ...);

/**
 * Returns the error-code of an error object.
 * @param[in] errObj  The error object
 * @return            The error-code
 */
int eo_code(const ErrObj* errObj);

/**
 * Returns the error object that happened immediately before an error object.
 * @param[in] errObj  The error object
 * @retval    NULL    No previous error
 * @return            The previous error object
 */
ErrObj* eo_prev(const ErrObj* errObj);

/**
 * Returns the error-code of an error object.
 * @param[in] errObj  The error object
 * @return            The associated error-code
 */
int eo_code(const ErrObj* errObj);

/**
 * Returns the name of the file in which an error object was created.
 * @param[in] errObj  The error object
 * @return            The name of the file
 */
const char* eo_file(const ErrObj* errObj);

/**
 * Returns the origin-1 line number associated with an error object.
 * @param[in] errObj  The error object
 * @return            The origin-1 line number
 */
int eo_line(const ErrObj* errObj);

/**
 * Returns the name of the function in which an error was created.
 * @param[in] errObj  The error object
 * @return            The name of the function
 */
const char* eo_func(const ErrObj* errObj);

/**
 * Returns the identifier of the thread on which an error object was created.
 * @param[in] errObj  The error object
 * @return            The identifier of the thread
 */
const pthread_t eo_thread(const ErrObj* errObj);

/**
 * Returns the message of an error object.
 * @param[in] errObj   The error object
 * @retval    NULL     No message
 * @return             The associated error-message
 */
const char* eo_msg(const ErrObj* errObj);

#ifdef __cplusplus
}
#endif

/**
 * Returns a new error-object.
 * @param[in] code  The error-code to associate with the error
 * @param[in] fmt   The format for the error message
 * @param[in] ...   The format's arguments
 */
#define EO_NEW(code, fmt, ...)    eo_new(__FILE__, __LINE__, __func__, code, fmt, __VA_ARGS__)

/**
 * Returns a new error-object caused by a system error. The error-object will only have this error.
 */
#define EO_SYSTEM() EO_NEW(errno, "%s", strerror(errno))

/**
 * Wraps a previously-occuring error with a later-occuring error.
 * @param[in] errObj  The previous error-object
 * @param[in] code    The error-code to associate with the error
 * @param[in] fmt     The format for the error message or `NULL`
 * @param[in] ...     The format's arguments
 */
#define EO_WRAP(errObj, code, fmt, ...) eo_wrap(errObj, _FILE_, _LINE_, __func__, code, fmt, \
        __VA_ARGS__)

/**
 * Wraps a previously-occuring error with a later-occuring error with no message.
 * @param[in] errObj  The previous error-object
 * @param[in] code    The error-code to associate with the error
 */
#define EO_WRAP_CODE(errObj, code) EO_WRAP(errObj, code, NULL)

/**
 * Wraps a previously-occuring error with a later-occuring error with the same error code.
 * @param[in] errObj  The previous error-object
 * @param[in] fmt     The format for the error message or `NULL`
 * @param[in] ...     The format's arguments
 */
#define EO_WRAP_MSG(errObj, fmt, ...) EO_WRAP(errObj, eo_code(errObj), fmt, __VA_ARGS__)

#endif /* MISC_ERROBJ_H_ */
