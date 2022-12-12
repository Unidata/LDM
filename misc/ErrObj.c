/**
 * This file defines an error-object. An error-object comprises a list of individual errors, from
 * the first (i.e., earliest) error to the last (i.e., most recent) error.
 *
 *        File: ErrObj.c
 *  Created on: Nov 17, 2022
 *      Author: Steven R. Emmerson
 */

#include "ErrObj.h"

#include <stdbool.h>
#include <stdlib.h>

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
struct ErrObj {
    Error* first;
    Error* last;
};

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
        error->line = line;
        error->func = strdup(func);

        if (error->func) {
            error->thread = pthread_self();
            error->code = code;
            int nbytes = vsnprintf(NULL, 0, fmt, args);

            if (nbytes >= 0) {
                error->msg = malloc(nbytes+1);

                if (error->msg) {
                    vsnprintf(error->msg, nbytes+1, fmt, args); // `nbytes >= 0` => can't fail
                    error->prev = error->next = NULL;
                    success = true;
                } // `error->msg` allocated
            } // `nbytes >= 0`

            if (!success)
                free(error->func);
        } // `error->func` allocated

        if (!success)
            free(error->file);
    } // `error->file` allocated

    return success;
}

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
    }
    return error;
}

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
    ErrObj errObj = NULL;
    va_list         args;

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

const Error* eo_first(const ErrObj* errObj)
{
    return errObj->first;
}

const Error* eo_next(const Error* error)
{
    return error->next;
}

const char* eo_file(const Error* error)
{
    return error->file;
}

int eo_line(const Error* error)
{
    return error->line;
}

const char* eo_func(const Error* error)
{
    return error->func;
}

const pthread_t eo_thread(const Error* error)
{
    return error->thread;
}

int eo_code(const Error* error)
{
    return error->code;
}

const char* eo_msg(const Error* error)
{
    return error->msg;
}
