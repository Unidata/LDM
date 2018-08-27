/**
 * This file declares a child command. A child command is a command that is
 * executed in a child process and to which the parent process can write to
 * the command's standard input stream and from which the parent procuess can
 * read the command's standard output stream. Lines that the command writes to
 * its standard error stream are automatically read by the parent process and
 * logged.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: ChildProcess.h
 *  Created on: Jul 10, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <stddef.h>
#include <sys/types.h>

#ifndef MISC_CHILDPROCESS_H_
#define MISC_CHILDPROCESS_H_

typedef struct child_cmd ChildCmd;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Executes a command in a child process.
 *
 * @param[in] pathname  Pathname of file to execute
 * @param[in] cmdVec    Command vector. Last element must be `NULL`.
 * @return              Resulting child command. Caller should call
 *                      `childCmd_reap()` on it to free resources.
 * @retval    NULL      Failure. `log_add()` called. `errno` is set:
 *                      - EAGAIN  The system lacked the necessary resources to
 *                                create another process, or the system-imposed
 *                                limit on the total number of processes under
 *                                execution system-wide or by a single user
 *                                {CHILD_MAX} would be exceeded.
 *                      - EAGAIN  The system lacked the necessary resources to
 *                                create another thread, or the system-imposed
 *                                limit on the total number of threads in a
 *                                process {PTHREAD_THREADS_MAX} would be
 *                                exceeded.
 *                      - EMFILE  {STREAM_MAX} streams are currently open in the
 *                                calling process.
 *                      - EMFILE  {FOPEN_MAX} streams are currently open in the
 *                                calling process.
 *                      - ENOMEM  Insufficient space to allocate a buffer.
 */
ChildCmd* childCmd_execvp(
        const char* const restrict pathname,
        const char* const restrict cmdVec[]);

/**
 * Waits for a child command to terminate. Releases all resources associated
 * with the child command.
 *
 * @param[in]  cmd         Child command
 * @param[out] exitStatus  Exit status of the child command
 * @return     0           Success. `*exitStatus` is set.
 * @retval     EINTR       The function was interrupted by a signal. The value
 *                         of the location pointed to by `exitStatus` is
 *                         undefined.
 *
 */
int childCmd_reap(
        ChildCmd* const cmd,
        int* const      exitStatus);

/**
 * Writes a line to the standard input stream of the child command.
 *
 * @param[in] cmd   Child command
 * @param[in] line  Line to be written
 * @return          Number of bytes written
 * @retval    -1    Failure. `log_add()` called. `errno` is set:
 *                    - EINVAL  Invalid child command
 */
ssize_t childCmd_putline(
        ChildCmd*   proc,
        char* const  line);

/**
 * Reads the next line from the standard output stream of a child command.
 *
 * @param[in]     cmd     Child command
 * @param[in,out] line    The next line or `NULL`. Must be appropriate to pass
 *                        to `realloc()`.
 * @param[in,out] nbytes  Size of `line`
 * @return                Number of bytes read, excluding the terminating NUL
 * @retval     -1         Failure. `log_add()` called. `errno` is set:
 *                          - EINVAL  Invalid child command
 */
ssize_t childCmd_getline(
        ChildCmd*     cmd,
        char** const  line,
        size_t* const nbytes);

/**
 * Executes a command in a child process with superuser privileges. Logs the
 * child's standard error stream. Waits for the child to terminate.
 *
 * @param[in]  cmdVec       Command vector. Last element must be `NULL`.
 * @param[out] childStatus  Exit status of the child process iff return value is
 *                          0
 * @retval     EAGAIN       The system lacked the necessary resources to create
 *                          another process, or the system-imposed on the total
 *                          number of processes under execution system-wide or
 *                          by a single user {CHILD_MAX} would be exceeded.
 *                          `log_add()` called.
 * @retval     EAGAIN       The system lacked the necessary resources to create
 *                          another thread, or the system-imposed limit on the
 *                          total number of threads in a process
 *                          {PTHREAD_THREADS_MAX} would be exceeded. `log_add()`
 *                          called.
 * @retval     EINTR        The function was interrupted by a signal that was
 *                          caught.  `log_add()` called.
 * @retval     EMFILE       {STREAM_MAX} streams are currently open in the
 *                          calling process. `log_add()` called.
 * @retval     EMFILE       {FOPEN_MAX} streams are currently open in the
 *                          calling process. `log_add()` called.
 * @retval     ENOMEM       Insufficient space to allocate a buffer. `log_add()`
 *                          called.
 * @retval     0            Success. `*childStatus` is set to the exit status
 *                          of the child process.
 */
int sudo(
        const char* const restrict cmdVec[],
        int* const restrict        childStatus);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_CHILDPROCESS_H_ */
