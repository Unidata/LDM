/**
 * This file implements a child command. A child command is a command that is
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
 *        File: ChildProc.c
 *  Created on: Jul 10, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "ChildCmd.h"
#include "log.h"
#include "mygetline.h"
#include "priv.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct child_cmd {
    const void*  magic;         ///< Verifies validity
    char*        cmdStr;        ///< Command string
    FILE*        stdIn;         ///< Child's standard input stream
    FILE*        stdOut;        ///< Child's standard output stream
    FILE*        stdErr;        ///< Child's standard error stream
    int          stdInPipe[2];  ///< Pipe for child's standard input stream
    int          stdOutPipe[2]; ///< Pipe for child's standard output stream
    int          stdErrPipe[2]; ///< Pipe for child's standard error stream
    pid_t        pid;           ///< PID of child process
    pthread_t    stdErrThread;  ///< Thread for logging child's stderr
};

static const int MAGIC;

/**
 * Logs the standard error stream of a child command.
 *
 * @param[in] cmd   Child command
 * @retval    NULL  Always
 */
static void*
childCmd_log(void* const arg)
{
    ChildCmd* const cmd = (ChildCmd*)arg;
    char*           line = NULL;
    size_t          size = 0;
    ssize_t         nbytes;

    while ((nbytes = mygetline(&line, &size, cmd->stdErr)) > 0) {
        if (line[nbytes-1] == '\n')
            line[nbytes-1] = 0;
        log_add("%s", line);
    }

    free(line);
    log_flush_notice(); // Might or might not be an error
    log_free();

    return NULL;
}

/**
 * Ensures that a file descriptor isn't one of the standard ones (i.e., 0, 1,
 * or 2). Modifies the descriptor if necessary.
 *
 * @param[in,out] fd  File descriptor. If a standard one, then it will be
 *                    duplicated, the original descriptor closed, and then set
 *                    to the duplicated value.
 * @retval 0          Success. `*fd` is guaranteed to be greater than 2.
 * @retval EMFILE     {OPEN_MAX} file descriptors are currently open in the
 *                    calling process, or no file descriptors greater than  2
 *                    are available
 */
static int
vetFd(int* fd)
{
	int              status;
	static const int MIN_FD = 3;

	if (*fd >= MIN_FD) {
		status = 0;
	}
	else {
		const int newFd = fcntl(*fd, F_DUPFD, MIN_FD);

		if (newFd == -1) {
			log_add_syserr("fcntl() failure");
			status = errno;
		}
		else {
			close(*fd);
			*fd = newFd;
			status = 0;
		}
	}

	return status;
}

/**
 * Returns a pipe with non-standard file descriptors (i.e., both file
 * descriptors of the pipe will be greater than 2).
 *
 * @param[out] fds     Vector for pipe's file descriptors
 * @retval     0       Success. `fds[0]` is read-end of pipe and `fds[1]` is
 *                     write-end. Both are greater than 2.
 * @retval     EMFILE  More than {OPEN_MAX} minus two file descriptors are
 *                     already in use by this process
 * @retval     EMFILE  {OPEN_MAX} file descriptors are currently open in the
 *                     calling process, or no file descriptors greater than or
 *                     equal to 3 are available
 * @retval     ENFILE  The number of simultaneously open files in the system
 *                     would exceed a system-imposed limit
 */
static int
getNonStdPipe(int fds[2])
{
	int status = pipe(fds);

	if (status) {
		log_add_syserr("pipe() failure");
		status = errno;
	}
	else {
		if ((status = vetFd(fds)) == 0)
			status = vetFd(fds+1);

		if (status) {
			close(fds[0]); fds[0] = -1;
			close(fds[1]); fds[1] = -1;
		}
	}

	return status;
}

/**
 * Returns a new child command structure.
 *
 * @return       New child process
 * @retval NULL  Failure. `log_add()` called. `errno` is set:
 *               - EMFILE  More than {OPEN_MAX} minus two file descriptors are
 *                 already in use by this process
 *               - EMFILE  {OPEN_MAX} file descriptors are currently open in the
 *                 calling process, or no file descriptors greater than or equal
 *                 to 3 are available
 *               - ENFILE  The number of simultaneously open files in the system
 *                 would exceed a system-imposed limit
 *               - ENOMEM  Insufficient space to allocate a buffer
 */
static ChildCmd*
childCmd_new(void)
{
    ChildCmd* cmd = log_malloc(sizeof(ChildCmd), "child command");

    if (cmd) {
        int status = getNonStdPipe(cmd->stdInPipe);

        if (status) {
            log_add_syserr("Couldn't create standard input pipe");
        }
        else {
            status = getNonStdPipe(cmd->stdOutPipe);

            if (status) {
                log_add_syserr("Couldn't create standard output pipe");
            }
            else {
                status = getNonStdPipe(cmd->stdErrPipe);

                if (status) {
                    log_add_syserr("Couldn't create standard error pipe");
                    (void)close(cmd->stdOutPipe[0]); cmd->stdOutPipe[0] = -1;
                    (void)close(cmd->stdOutPipe[1]); cmd->stdOutPipe[1] = -1;
                }
                else {
                    (void)memset(&cmd->stdErrThread, 0,
                            sizeof(cmd->stdErrThread));

                    cmd->cmdStr = NULL;
                    cmd->stdIn = NULL;
                    cmd->stdOut = NULL;
                    cmd->stdErr = NULL;
                    cmd->magic = &MAGIC;
                }
            } // `cmd->stdOutPipe` open

            if (status) {
                (void)close(cmd->stdInPipe[0]); cmd->stdInPipe[0] = -1;
                (void)close(cmd->stdInPipe[1]); cmd->stdInPipe[1] = -1;
            }
        } // `cmd->stdInPipe` open

        if (status) {
            free(cmd);
            cmd = NULL;
        }
    } // `cmd` allocated

    return cmd;
}

/**
 * Concatenates a command vector into a command string.
 *
 * @param[in] cmdVec  Command vector. Last element must be `NULL`.
 * @retval    NULL    Failure. `log_add()` called. `errno` is set.
 * @return            Command string. Caller should free when it's no longer
 *                    needed.
 */
static char*
catCmdVec(const char* const restrict cmdVec[])
{
    int    numArgs;
    size_t nbytes = 0;
    static const char whiteSpace[] = " \t";

    for (numArgs = 0; cmdVec[numArgs] != NULL; ++numArgs) {
        const char* const arg = cmdVec[numArgs];

        nbytes += strlen(arg) + 1; // Plus space separator

        if (strpbrk(arg, whiteSpace) != NULL)
            nbytes += 2; // For quotes
    }

    ++nbytes; // Trailing NUL byte

    char* cmd = log_malloc(nbytes, "Command buffer");

    if (cmd) {
        char* cp = cmd;

        for (int i = 0; i < numArgs; ++i) {
            const char* const arg = cmdVec[i];
            const bool        needsQuotes = strpbrk(arg, whiteSpace) != NULL;
            const char* const fmt = needsQuotes ? "'%s' " : "%s ";

            cp += sprintf(cp, fmt, arg);
        }

        if (cp != cmd)
            --cp; // To stomp trailing space

        *cp = 0; // Stomp trailing space or create empty string
    }

    return cmd;
}

static void
childCmd_free(ChildCmd* const cmd)
{
    if (cmd) {
        if (cmd->stdErrPipe[0] >= 0) {
            (void)close(cmd->stdErrPipe[0]); cmd->stdErrPipe[0] = -1;
        }
        if (cmd->stdErrPipe[1] >= 0) {
            (void)close(cmd->stdErrPipe[1]); cmd->stdErrPipe[1] = -1;
        }
        if (cmd->stdOutPipe[0] >= 0) {
            (void)close(cmd->stdOutPipe[0]); cmd->stdOutPipe[0] = -1;
        }
        if (cmd->stdOutPipe[1] >= 0) {
            (void)close(cmd->stdOutPipe[1]); cmd->stdOutPipe[1] = -1;
        }
        if (cmd->stdInPipe[0] >= 0) {
            (void)close(cmd->stdInPipe[0]); cmd->stdInPipe[0] = -1;
        }
        if (cmd->stdInPipe[1] >= 0) {
            (void)close(cmd->stdInPipe[1]); cmd->stdInPipe[1] = -1;
        }
        cmd->magic = NULL;
        free(cmd->cmdStr);
        free(cmd);
    }
}

/**
 * Indicates if a child command is valid or not.
 *
 * @param[in] cmd      Child command
 * @retval    `true`   Child command is valid
 * @retval    `false`  Child command is not valid. `log_add()` called.
 */
inline static bool
isValid(const ChildCmd* const cmd)
{
    const bool valid = cmd != NULL && cmd->magic == &MAGIC;

    if (!valid)
        log_add("Child command is not valid");

    return valid;
}

/**
 * Executes a command in a child process.
 *
 * @param[in] cmd       Child command structure
 * @param[in] pathname  Pathname of file to execute
 * @param[in] cmdVec    Command vector
 * @param[in] asRoot    Execute command as root?
 * @retval    0         Success
 * @retval    EAGAIN    The system lacked the necessary resources to create
 *                      another process, or the system-imposed limit on the
 *                      total number of processes under execution system-wide or
 *                      by a single user {CHILD_MAX} would be exceeded.
 * @retval    EAGAIN    The system lacked the necessary resources to create
 *                      another thread, or the system-imposed limit on the total
 *                      number of threads in a process {PTHREAD_THREADS_MAX}
 *                      would be exceeded.
 * @retval    ENOMEM    Insufficient space to allocate a buffer.
 */
static int
execute(
        ChildCmd* const restrict   cmd,
        const char* const restrict pathname,
        const char* const restrict cmdVec[],
        const bool                 asRoot)
{
    int         status;
	const pid_t pid = fork();

	if (pid == -1) {
		log_add_syserr("fork() failed");
		status = errno;
	}
	else if (pid == 0) {
		// Child process

		// The following file descriptors are unneeded
		(void)close(cmd->stdInPipe[1]);
		(void)close(cmd->stdOutPipe[0]);
		(void)close(cmd->stdErrPipe[0]);

		/*
		 * All file-descriptors in the pipes are guaranteed to be greater than
		 * 2.
		 */
		(void)dup2(cmd->stdInPipe[0], STDIN_FILENO);
		(void)close(cmd->stdInPipe[0]); // No longer needed

		(void)dup2(cmd->stdOutPipe[1], STDOUT_FILENO);
		(void)close(cmd->stdOutPipe[1]); // No longer needed

		(void)dup2(cmd->stdErrPipe[1], STDERR_FILENO);
		(void)close(cmd->stdErrPipe[1]); // No longer needed

		uid_t euid;

		if (!asRoot) {
			status = 0;
		}
		else {
			euid = geteuid();
			status = seteuid(0); // Get privilege to set real UID to root

			if (status) {
				log_add_syserr("Couldn't get root privilege");
			}
			else {
				status = setuid(0);

				if (status)
					log_add_syserr("Couldn't set UID to root");
			}
		}

		if (status == 0) {
			log_debug("Executing command \"%s\"", cmd->cmdStr);
			(void)execvp(pathname, (char* const*)cmdVec);

			if (asRoot)
				(void)seteuid(euid);

			log_add_syserr("execvp() failed");
			log_flush_error();
			log_fini();
			exit(1);
		} // Executing as root
	}
	else {
		// Parent process
		(void)close(cmd->stdInPipe[0]);  // Read end of stdin pipe unneeded
		(void)close(cmd->stdOutPipe[1]); // Write end of stdout pipe unneeded
		(void)close(cmd->stdErrPipe[1]); // Write end of stderr pipe unneeded

		cmd->stdInPipe[0] = -1;
		cmd->stdOutPipe[1] = -1;
		cmd->stdErrPipe[1] = -1;

		cmd->stdIn = fdopen(cmd->stdInPipe[1], "w");

		if (cmd->stdIn == NULL) {
			log_add_syserr("fdopen() failure");
			status = errno;
		}
		else {
			cmd->stdOut = fdopen(cmd->stdOutPipe[0], "r");

			if (cmd->stdOut == NULL) {
				log_add_syserr("fdopen() failure");
				status = errno;
			}
			else {
				cmd->stdErr = fdopen(cmd->stdErrPipe[0], "r");

				if (cmd->stdErr == NULL) {
					log_add_syserr("fdopen() failure");
					status = errno;
				}
				else {
					status = pthread_create(&cmd->stdErrThread, NULL,
							childCmd_log, cmd);

					if (status) {
						log_add_errno(status, "Couldn't create thread to "
								"log child's standard-error stream");
					}
					else {
						cmd->pid = pid;
					} // `stdErrThread` set

					if (status) {
						(void)fclose(cmd->stdErr);
						cmd->stdErrPipe[0] = -1;
					}
				} // `proc->stdErr` allocated

				if (status) {
					(void)fclose(cmd->stdOut);
					cmd->stdOutPipe[0] = -1;
				}
			} // `proc->stdOut` allocated

			if (status) {
				(void)fclose(cmd->stdIn);
				cmd->stdInPipe[1] = -1;
			}
		} // `proc->stdIn` allocated
	} // Parent process. `pid` set.

    return status;
}

/**
 * Spawns a child process and executes a command
 *
 * @param[in] cmd       Child command structure
 * @param[in] pathname  Pathname of file to execute
 * @param[in] cmdVec    Command vector
 * @param[in] asRoot    Execute command as root?
 * @retval    NULL      Failure. `log_add()` called.
 * @return              Child command
 */
static ChildCmd*
spawn(  const char* const restrict pathname,
        const char* const restrict cmdVec[],
        const bool                 asRoot)
{
    ChildCmd* cmd = childCmd_new();

    if (cmd) {
        int status;

        cmd->cmdStr = catCmdVec(cmdVec);

        if (cmd->cmdStr == NULL) {
            log_add("Couldn't concatenate command \"%s\" arguments", pathname);

            status = -1;
        }
        else {
            status = execute(cmd, pathname, cmdVec, asRoot);

            if (status) {
                errno = status;
                log_add("Couldn't execute command \"%s\"", cmd->cmdStr);
            }
        } // `cmd->cmdStr` allocated

        if (status) {
            childCmd_free(cmd);
            cmd = NULL;
        }
    } // `cmd` allocated

    return cmd;
}

ChildCmd*
childCmd_execvp(
        const char* const restrict pathname,
        const char* const restrict cmdVec[])
{
    return spawn(pathname, cmdVec, false);
}

int
childCmd_reap(
        ChildCmd* const cmd,
        int* const      exitStatus)
{
    int status;

    if (!isValid(cmd)) {
        status = EINVAL;
    }
    else {
        status = waitpid(cmd->pid, exitStatus, 0);

        if (status != cmd->pid) {
            status = errno;
            log_add_syserr("waitpid() failed");
        }
        else {
            (void)pthread_join(cmd->stdErrThread, NULL);

            *exitStatus = WEXITSTATUS(*exitStatus);

            if (*exitStatus)
                log_add("Command \"%s\" exited with status %d", cmd->cmdStr,
                        *exitStatus);

            childCmd_free(cmd);
            status = 0;
        }
    }

    return status;
}

ssize_t
childCmd_putline(
        ChildCmd*   cmd,
        char* const line)
{
    ssize_t status;

    if (!isValid(cmd)) {
        errno = EINVAL;
        status = -1;
    }
    else {
        status = fputs(line, cmd->stdIn);

        if (status == EOF)
            log_add_syserr("Couldn't write to standard input of command \"%s\"",
                    cmd->cmdStr);
    }

    return status;
}

ssize_t
childCmd_getline(
        ChildCmd*     cmd,
        char** const  line,
        size_t* const size)
{
    ssize_t status;

    if (!isValid(cmd)) {
        errno = EINVAL;
        status = -1;
    }
    else {
        status = mygetline(line, size, cmd->stdOut);

        if (status == -1) {
            // Invalid arguments, EOF, or I/O error
            if (ferror(cmd->stdOut)) {
                log_add("mygetline() failure on stdout of command \"%s\"",
                        cmd->cmdStr);
            }
            else {
                status = 0; // EOF
            }
        }
    }

    return status;
}

const char*
childCmd_getCmd(ChildCmd* const cmd)
{
    return isValid(cmd)
            ? cmd->cmdStr
            : NULL;
}

int
sudo(   const char* const restrict cmdVec[],
        int* const restrict        childStatus)
{
    int       status;
    ChildCmd* cmd = spawn(cmdVec[0], cmdVec, true);

    if (cmd == NULL) {
        status = errno;
    }
    else {
        status = childCmd_reap(cmd, childStatus);

        if (status)
            log_add("Couldn't reap command \"%s\"", cmd->cmdStr);
    } // `cmd` allocated

    return status;
}
