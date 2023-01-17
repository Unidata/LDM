/**
 * This file defines an error-object. An error-object comprises a sequence of individual errors,
 * from the first (i.e., earliest) error to the last (i.e., most recent) error.
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

/// An individual error
struct Error {
    Error*    prev;
    Error*    next;
    char*     file;
    int       line;
    char*     func;
    pthread_t thread;
    int       code;
    char*     msg;
};
/// An error object
struct ErrObj {
    Error* first;
    Error* last;
};

/**
 * Initializes an error.
 * @param[in] error    The error to be initialized
 * @param[in] file     The name of the file in which the error occurred
 * @param[in] line     The line number in the file to associate with the error
 * @param[in] func     The name of the function in which the error occurred
 * @param[in] code     The error's code
 * @param[in] fmt      The format for the error message
 * @param[in] args     The format's arguments
 * @retval    `true`   Success
 * @retval    `false`  Failure
 */
static bool err_init(
        Error*      error,
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        va_list     args)
{
    bool success = false;
    error->file = strdup(file);

    if (error->file) {
        error->func = strdup(func);

        if (error->func) {
            const int nbytes = vsnprintf(NULL, 0, fmt, args);

            if (nbytes >= 0) {
                error->msg = malloc(nbytes+1);

                if (error->msg) {
                    // `nbytes >= 0` => can't fail
                    (void)vsnprintf(error->msg, nbytes+1, fmt, args); // Calls `va_start(args)`
                    error->thread = pthread_self();
                    error->line = line;
                    error->code = code;
                    error->prev = error->next = NULL;
                    success = true;
                } // Message buffer allocated
            } // Message can be printed

            if (!success)
                free(error->func);
        } // Function name allocated

        if (!success)
            free(error->file);
    } // Filename allocated

    return success;
}

/**
 * Returns a new error.
 * @param[in] error    The error to be initialized
 * @param[in] file     The name of the file in which the error occurred
 * @param[in] line     The line number in the file to associate with the error
 * @param[in] func     The name of the function in which the error occurred
 * @param[in] code     The error's code
 * @param[in] fmt      The format for the error message
 * @param[in] args     The format's arguments
 * @retval    NULL     Failure
 * @return             A new error
 */
static Error* err_new(
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        va_list     args)
{
    Error* error = malloc(sizeof(Error));
    if (error) {
        if (!err_init(error, file, line, func, code, fmt, args)) {
            free(error);
            error = NULL;
        }
    } // Error allocated
    return error;
}

/**
 * Deletes an error.
 * @param[in] error  The error to be deleted
 */
static void err_delete(Error* error)
{
    if (error) {
        free(error->msg);
        free(error->func);
        free(error->file);
        free(error);
    }
}

ErrObj* eo_new(
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        ...)
{
    ErrObj* errObj = NULL;
    va_list          args;

    va_start(args, fmt);
    Error* error = err_new(file, line, func, code, fmt, args);
    va_end(args);

    if (error) {
        errObj = malloc(sizeof(ErrObj));
        if (errObj == NULL) {
            err_delete(error);
        }
        else {
            errObj->first = errObj->last = error;
        } // `errObj` allocated
    }

    return errObj;
}

void eo_delete(ErrObj* errObj)
{
    if (errObj) {
        for (Error* error = errObj->first; error; ) {
            Error* next = error->next;
            err_delete(error);
            error = next;
        }
        free(errObj);
    }
}

ErrObj* eo_add(
        ErrObj*     errObj,
        const char* file,
        const int   line,
        const char* func,
        const int   code,
        const char* fmt,
        ...)
{
    va_list         args;

    va_start(args, fmt);
    Error* error = err_new(file, line, func, code, fmt, args);
    va_end(args);

    if (error == NULL)
        return NULL;

    errObj->last->next = error;
    error->prev = errObj->last;
    errObj->last = error;

    return errObj;
}

const Error* er_first(const ErrObj* errObj)
{
    return errObj->first;
}

const Error* er_next(const Error* error)
{
    return error->next;
}

const Error* er_last(const ErrObj* errObj)
{
    return errObj->last;
}

const Error* er_prev(const Error* error)
{
    return error->prev;
}

const char* er_file(const Error* error)
{
    return error->file;
}

int er_line(const Error* error)
{
    return error->line;
}

const char* er_func(const Error* error)
{
    return error->func;
}

const pthread_t er_thread(const Error* error)
{
    return error->thread;
}

int er_code(const Error* error)
{
    return error->code;
}

const char* er_msg(const Error* error)
{
    return error->msg;
}
