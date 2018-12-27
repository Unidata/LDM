LDM Logging             {#mainpage}
===========

Copyright 2016 University Corporation for Atmospheric Research. All rights
reserved. See the the file COPYRIGHT in the top-level source-directory for
licensing conditions.

@author: Steven R. Emmerson

@section contents Table of Contents
- \ref introduction
- \ref example
- \ref format

<hr>

@section introduction Introduction
The logging component of the LDM comprises a single API
with two implementations: one using a simple implementation and the other
using the LDM's original `ulog` module (that module is still part of the LDM
library for backward compatibility with user-developed code). By default the
simple implementation is used. The `ulog` implementation will be used if the
option `--with-ulog` is given to the `configure` script.

Unless otherwise stated, this documentation is primarily on the simple
implementation rather than the `ulog` implementation, which is documented
elsewhere.

Both implementations manage a FIFO queue of log messages for each thread in a
process. `log_add*()` functions add to the queue. At some point, one of the
following should occur:
  - A final message optionally added;
  - The accumulated messages emitted by one of the `log_flush_*()` functions
    (e.g., "log_flush_error()"); or
  - The queue cleared by `log_clear()`.

By default, log messages are written to
  - If the process is a daemon (i.e., doesn't have a controlling terminal)
    - Simple Implementation: The LDM log file
    - `ulog` implementation: The system logging daemon
  - If the process is not a daemon: The standard error stream

(Note that the LDM server, `ldmd`, daemonizes itself by default. It is the
only program that does.)

The default destination for log messages can usually be overridden by the
command-line option `-l` _dest_:
@par
<em>dest</em>   | Destination
--------------- | -----------------------------
<tt>""</tt>     | System logging daemon
<tt>"-"</tt>    | Standard error stream
<em>path</em>   | File whose pathname is _path_

Besides managing thread-specific queues of log messages, the LDM logging
component also registers a handler for the `USR1` signal. If log messages are
being written to a regular file (e.g., the LDM log file), then upon receipt of
the signal, the LDM logging component will refresh (i.e., close and re-open) its
connection to the file. This allows the log files to be rotated and purged by an
external process so that the disk partition doesn't become full.

---------------

@section example Example
Here's a contrived example:

@code{.c}
#include <log.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static int system_failure()
{
    (void)close(-1); // Guaranteed failure for the purpose of this example
    log_add_syserr("close() failure"); // Uses `errno`; adds to message queue
    return -1;
}

static int func()
{
    int status = system_failure();
    if (status)
        log_add("system_failure() returned %d", status); // Adds to message queue
    return status;
}

static pid_t exec_child(const char* const path)
{
    int pid = fork();
    if (pid < 0) {
        log_add_syserr("Couldn't fork() process"); // Uses `errno`
    }
    else if (pid > 0) {
        // Child process
        (void)execl(path, path, NULL)
        /*
         * Adds to empty message queue using `errno`, prints queue at ERROR
         * level, then clears queue:
         */
        log_add_syserr("execl(\"%s\") failure", path); // Uses `errno`
        log_flush_error();
        log_fini(); // Good form
        exit(1);
    }
    return pid;
}

// Called by pthread_create()
static void* start(void* arg)
{
    ...
    log_free(); // Frees thread-specific resources
    return NULL;
}

static int daemonize(void)
{
    int   status;
    pid_t pid = fork();
    if (pid == -1) {
        log_add_syserr("fork() failure"); // Uses `errno`
        status = -1;
    }
    else {
        if (pid > 0) {
            // Parent process
            (void)printf("%ld\n", (long)pid);
            status = 0;
        }
        else {
            // Child process
            (void)setsid();
            (void)fclose(stdin);
            (void)fclose(stdout);
            (void)fclose(stderr);
            log_avoid_stderr(); // Because this process is now a daemon
            ...
            status = 0;
        }
    }
    return status;
}

int main(int argc, char* argv)
{
    ...
    log_init(argv[0]); // Necessary
    ...
    while ((int c = getopt(argc, argv, "l:vx") != EOF) {
        extern char *optarg;
        switch (c) {
            case 'l':
                 (void)log_set_output(optarg);
                 break;
            case 'v':
                 // In case "-x" option specified first (e.g., "-xv")
                 if (!log_is_enabled_info)
                     (void)log_set_level(LOG_LEVEL_INFO);
                 break;
            case 'x':
                 (void)log_set_level(LOG_LEVEL_DEBUG);
                 break;
            ...
        }
    }
    ...
    if (func()) {
        if (log_is_enabled_info) { // Test because of `slow_func()`
            // Adds to message queue, prints queue at INFO level, clears queue
            log_add("func() failure: reason = %s", slow_func());
            log_flush_info();
        }
    }

    if (func()) {
        // Adds to message queue, prints queue at ERROR level, clears queue
        log_add("func() failure: reason = %s", fast_func());
        log_flush_error();
    }

    const char program[] = "utility";
    if (exec_child(program) < 0) {
        // Adds to message queue, prints queue at ERROR level, clears queue
        log_add("Couldn't execute program %s", program);
        log_flush_error();
    }
    
    pthread_t thread_id;
    int       status = pthread_create(&thread_id, NULL, start, NULL);
    if (status) {
        // Adds to message queue, prints queue at ERROR level, clears queue
        log_add("Couldn't create thread");
        log_flush_error();
    }
    
    status = daemonize();
    if (status) {
        // Adds to message queue, prints queue at ERROR level, clears queue
        log_add("Couldn't daemonize");
        log_flush_error();
    }
    ...
    log_fini(); // Good form
    return status ? 1 : 0;
}
@endcode

<hr>

@section format Format of Log Messages

Log messages sent to either the standard error stream or the LDM log file by
the simple implementation will have the following format:

> _time_ _proc_ _loc_ _level_ _msg_

where:
<dl>
<dt><em>time</em> <dd>Is the creation-time of the message in the form
    <em>YYYYMMDD</em>T<em>hhmmss</em>.<em>uuuuuu</em>Z
    (e.g., `20160121T163218.391847Z`).
<dt><em>proc</em> <dd>Is the identifier of the process in the form
    <em>id</em>[<em>pid</em>], where <em>id</em> is the identifier given to
    log_init(), log_set_id(), or log_set_upstream_id(), and <em>pid</em> is the
    system's numeric process-identifier.
<dt><em>loc</em> <dd>Is the location where the message was created in the form
    <em>file</em>:<em>func</em>, where <em>file</em> and <em>func</em> are,
    respectively, the names of the file and function that generated the message.
<dt><em>level</em> <dd>Is the logging-level (i.e., priority) of the message. One
    of `DEBUG`, `INFO`, `NOTE`, `WARN`, or `ERROR`.
<dt><em>msg</em> <dd>Is the actual message given to one of the logging
    functions.
</dl>

Log messages sent to the system logging daemon will, in general, have the same
_proc_, _loc_, and _msg_ components as above, but the _time_ and _level_ 
components will depend on the system logging daemon. Additionally, the system
logging daemon might alter the order of the components and/or add additional
components (e.g., the name of the local host).
