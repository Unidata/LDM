/**
 * This file defines an error-object. An error-object comprises a sequence of individual errors,
 * from the earliest error to the most recent error.
 *
 *        File: ErrObj.c
 *  Created on: Nov 17, 2022
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "ErrObj.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// An error object
struct ErrObj {
    ErrObj*   prev;   ///< The previous error object
    char*     file;   ///< The name of the file in which the error object was created
    char*     func;   ///< The name of the function in which the error object was created
    char*     msg;    ///< The error message
    pthread_t thread; ///< The thread on which the error object was created
    int       line;   ///< The line number where the error object was created
    int       code;   ///< The error code
};

/**
 * Initializes an error object.
 * @param[in] errObj   The error object to be initialized
 * @param[in] file     The name of the file in which the error occurred
 * @param[in] line     The line number in the file to associate with the error
 * @param[in] func     The name of the function in which the error occurred
 * @param[in] code     The error's code
 * @param[in] fmt      The format of the error message or `NULL`
 * @param[in] args     The format's arguments. Ignored if the format is `NULL`.
 * @param[in] prev     The previous error object to be wrapped or `NULL`
 * @retval    `true`   Success
 * @retval    `false`  Failure
 */
static bool init(
        ErrObj*     const restrict errObj,
        const char* const restrict file,
        const int                  line,
        const char* const restrict func,
        const int                  code,
        const char* const restrict fmt,
        va_list                    args,
        ErrObj* const restrict     prev)
{
    bool success = false;

    errObj->thread = pthread_self();
    errObj->line = line;
    errObj->code = code;
    errObj->prev = prev;
    errObj->file = strdup(file);

    if (errObj->file) {
        errObj->func = strdup(func);

        if (errObj->func) {
            if (fmt == NULL) {
                errObj->msg = NULL;
                success = true;
            }
            else {
                const int nbytes = vsnprintf(NULL, 0, fmt, args);

                if (nbytes >= 0) {
                    errObj->msg = malloc(nbytes+1);

                    if (errObj->msg) {
                        // nbytes >= 0 => the following can't fail
                        (void)vsnprintf(errObj->msg, nbytes+1, fmt, args); // Calls va_start(args)

                        success = true;
                    } // Message buffer allocated
                } // Have size of message
            } // Format isn't NULL

            if (!success)
                free(errObj->func);
        } // Function name allocated

        if (!success)
            free(errObj->file);
    } // Filename allocated

    return success;
}

/**
 * Constructs (i.e., allocates and initializes) an error object.
 * @param[in] file  The name of the file in which the error object was created
 * @param[in] line  The line number where the error object was created
 * @param[in] func  The name of the function in which the error object was created
 * @param[in] code  The error code
 * @param[in] fmt   The format of the error message or `NULL`
 * @param[in] args  The format's arguments. Ignored if the format is `NULL`.
 * @param[in] prev  The prevously-occuring error to be wrapped or `NULL`
 * @retval    NULL  Out of memory
 * @return          An error object
 */
static ErrObj* construct(
        const char* const restrict file,
        const int                  line,
        const char* const restrict func,
        const int                  code,
        const char* const restrict fmt,
        va_list                    args,
        ErrObj* const restrict     prev)
{
    ErrObj* errObj = malloc(sizeof(ErrObj));

    if (errObj && !init(errObj, file, line, func, code, fmt, args, prev)) {
        free(errObj);
        errObj = NULL;
    } // Error allocated

    return errObj;
}

ErrObj* eo_new(
        const char* restrict file,
        const int            line,
        const char* restrict func,
        const int            code,
        const char* restrict fmt,
        ...)
{
    va_list args;

    va_start(args, fmt);
    ErrObj* errObj = construct(file, line, func, code, fmt, args, NULL);
    va_end(args);

    return errObj;
}

void eo_delete(ErrObj* const errObj)
{
    if (errObj) {
        eo_delete(errObj->prev);

        free(errObj->msg); // NULL safe
        free(errObj->func);
        free(errObj->file);
        free(errObj);
    }
}

ErrObj* eo_wrap(
        ErrObj* restrict     errObj,
        const char* restrict file,
        const int            line,
        const char* restrict func,
        const int            code,
        const char* restrict fmt,
        ...)
{
    va_list args;

    va_start(args, fmt);
    ErrObj* newErrObj = construct(file, line, func, code, fmt, args, errObj);
    va_end(args);

    return newErrObj;
}

ErrObj* eo_prev(const ErrObj* const errObj)
{
    return errObj->prev;
}

const char* eo_file(const ErrObj* const errObj)
{
    return errObj->file;
}

int eo_line(const ErrObj* const errObj)
{
    return errObj->line;
}

const char* eo_func(const ErrObj* const errObj)
{
    return errObj->func;
}

const pthread_t eo_thread(const ErrObj* const errObj)
{
    return errObj->thread;
}

int eo_code(const ErrObj* const errObj)
{
    return errObj->code;
}

const char* eo_msg(const ErrObj* const errObj)
{
    return errObj->msg; // Can be NULL
}
