/**
 * This file declares an error-object. An error-object comprises a list of individual errors.
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

typedef struct Error  Error;
typedef struct ErrObj ErrObj;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a new error-object. The error-object will contain a single error.
 *
 * @param[in] file  The name of the file in which the error occurred
 * @param[in] line  The line number in the file to associate with the error
 * @param[in] func  The name of the function in which the error occurred
 * @param[in] code  The error's code
 * @param[in] fmt   The format for the error message
 * @param[in] ...   The error message's arguments
 * @retval    NULL  Out-of-memory
 * @return          The new error-object
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
 *
 * @param[in] errObj  The error-object to be deleted
 */
void eo_delete(ErrObj* errObj);

/**
 * Adds a later-occuring error to an error-object.
 *
 * @param[in] errObj  The error-object to be added to
 * @param[in] file    The name of the file in which the error occurred
 * @param[in] line    The line number in the file to associate with the error
 * @param[in] func    The name of the function in which the error occurred
 * @param[in] code    The error's code
 * @param[in] fmt     The format for the error message
 * @param[in] ...     The error message's arguments
 * @retval    errObj  Success
 * @retval    NULL    Out-of-memory
 */
ErrObj* eo_add(
        ErrObj*     errObj,
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        ...);

/**
 * Returns the first (i.e., earliest) error of an error-object.
 *
 * @param[in] errObj  The error-object
 * @return            The first error
 */
const Error* eo_first(const ErrObj* errObj);

/**
 * Returns the error that happened immediately after an error.
 *
 * @param[in] error   The error
 * @retval    NULL    No more errors
 * @return            The next error after the given one
 */
const Error* eo_next(const Error* error);

/**
 * Returns the name of the file in which an error occurred.
 *
 * @param[in] error  The error
 * @return           The name of the file
 */
const char* eo_file(const Error* error);

/**
 * Returns the origin-1 line number associated with an error.
 *
 * @param[in] error  The error
 * @return           The origin-1 line number
 */
int eo_line(const Error* error);

/**
 * Returns the name of the function in which an error occurred.
 *
 * @param[in] error  The error
 * @return           The name of the function
 */
const char* eo_func(const Error* error);

/**
 * Returns the identifier of the thread on which an error was created.
 *
 * @param[in] error  The error
 * @return           The identifier of the thread
 */
const pthread_t eo_thread(const Error* error);

/**
 * Returns the error-code of an error.
 *
 * @param[in] error  The error
 * @return           The associated error-code
 */
int eo_code(const Error* error);

/**
 * Returns the message of an error.
 *
 * @param[in] errObj  The error
 * @return            The associated error-message
 */
const char* eo_msg(const Error* errObj);

#ifdef __cplusplus
}
#endif

/**
 * Returns a new error-object.
 *
 * @param[in] code  The error-code to associate with the error
 * @param[in] fmt   The format for the error message
 * @param[in] ...   The error message's arguments
 */
#define EO_NEW(code, fmt, ...)    eo_new(__FILE__, __LINE__, __func__, code, fmt, __VA_ARGS__)

/**
 * Returns a new error-object caused by a system error. The error-object will only have this error.
 */
#define EO_SYSTEM() EO_NEW(errno, "%s", strerror(errno))

/**
 * Adds a later-occuring error to an error-object.
 *
 * @param[in] errObj  The error-object to be added to
 * @param[in] code    The error-code to associate with the error
 * @param[in] fmt     The format for the error message
 * @param[in] ...     The error message's arguments
 */
#define EO_ADD(errObj, code, fmt, ...) eo_add(errObj, _FILE_, _LINE_, __func__, code, fmt, __VA_ARGS__)

#endif /* MISC_ERROBJ_H_ */
