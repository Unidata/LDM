/*
 *   Copyright 2016 University Corporation for Atmospheric Research
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>

#include "child_map.h"
#include "ldm.h"
#include "ldmalloc.h"
#include "ldmfork.h"
#include "action.h"
#include "error.h"
#include "filel.h"
#include "globals.h"
#include "remote.h"
#include "pq.h"
#include "log.h"

ChildMap*        execMap = NULL;


/*ARGSUSED*/
static int
prod_noop(
        const product* const restrict prod,
        const int                     argc,
        char** const restrict         argv,
        const void*                   xprod,
        const size_t                  xlen)
{
        return 0;
}


/**
 * Execute a program.
 *
 * @param[in] prod    Pointer to the data-product that caused this action.
 * @param[in] argc    Number of arguments in the command-line.
 * @param[in] argv    Pointer to pointers to command-line arguments.
 * @param[in] xprod   Pointer to XDR-ed data-product.
 * @param[in] xlen    Size of "xprod" in bytes.
 * @retval    -1      Failure.  An error--message is logged.
 * @retval     0      Success.
 */
/*ARGSUSED*/
static int
exec_prodput(
     const product* const restrict prod,
     int                           argc,
     char** restrict               argv,
     const void* const restrict    xprod,
     const size_t                  xlen)
{
    pid_t       pid = 0;

    if (NULL == execMap) {
        // Child-process map not allocated
        execMap = cm_new();

        if (NULL == execMap) {
            log_error_q("Couldn't create child-process map for EXEC entries");
            pid = -1;
        }
    }

    if (0 == pid) {
        int waitOnChild = 0; // Default is not to wait

        if (strcmp(argv[0], "-wait") == 0) {
            waitOnChild = 1;
            argc--; argv++;
        }

        pid = ldmfork();
        if (-1 == pid) {
            log_add_syserr("Couldn't fork EXEC process");
            log_flush_error();
        }
        else if (0 == pid) {
            // Child process.

#if WANT_SETPGID_EXEC
            /*
             * Make this process a process-group leader so that it doesn't
             * receive signals sent to the LDM's process group (e.g., SIGCONT,
             * SIGTERM, SIGINT, SIGUSR1, SIGUSR2)
             */
            if (setpgid(0, 0) == -1) {
                log_warning("Couldn't make EXEC program \"%s\" a "
                        "process-group leader", argv[0]);
            }
#endif

            (void)signal(SIGTERM, SIG_DFL);
            (void)pq_close(pq);

            /*
             * It is assumed that the standard input, output, and error
             * streams are correct and should not be modified.
             */

            // Don't let the child process get any inappropriate privileges.
            endpriv();
            log_info_q("Executing program \"%s\"", argv[0]);
            (void)execvp(argv[0], argv);
            log_syserr("Couldn't execute utility \"%s\"; PATH=%s", argv[0],
                    getenv("PATH"));
            exit(EXIT_FAILURE); // cleanup() calls log_fini()
        }
        else {
            // Parent process.
            (void)cm_add_argv(execMap, pid, argv);

            if (!waitOnChild) {
                log_debug("exec %s[%d]", argv[0], pid);
            }
            else {
                log_debug("exec -wait %s[%d]", argv[0], pid);
                (void)reap(pid, 0);
            }
        }
    } // Child-process map allocated

    return -1 == pid ? -1 : 0;
}


int
atoaction(
        const char *str,
        actiont *resultp)
{
#define MAXACTIONLEN 12
        char buf[MAXACTIONLEN];
        const char *in;
        char *cp;
        actiont *ap;

        static actiont assoc[] = {
                {"noop",
                        0,
                        prod_noop},
                {"file",
                        0,
                        unio_prodput},
                {"stdiofile",
                        0,
                        stdio_prodput},
                {"dbfile",
                        0,
#ifndef NO_DB
                        ldmdb_prodput},
#else
                        prod_noop},
#endif
                {"pipe",
                        0,
                        pipe_prodput},
                {"spipe",
                        0,
                        spipe_prodput},
                {"xpipe",
                        0,
                        xpipe_prodput},
                {"exec",
                        0,
                        exec_prodput},

        };

        if(str == NULL || *str == 0)
        {
                log_debug("atoaction: Invalid string argument");
                return -1;
        }

        for(in = str, cp = buf; *in != 0 && cp < &buf[MAXACTIONLEN] ; cp++, in++)
        {
                *cp = (char)(isupper(*in) ? tolower((*in)) : (*in));
        }
        *cp = 0;

        for(ap = &assoc[0]; ap < &assoc[sizeof(assoc)/sizeof(assoc[0])] ; ap ++)
        {
                if(strcmp(ap->name, buf) == 0)
                {
                        (void) memcpy((char *)resultp, (char *)ap,
                                      sizeof(actiont));
                        return 0;
                }
        }
        /* uerror("Unknown action \"%s\"", str); */
        return -1; /* noop */
}


char *
s_actiont(actiont *act)
{
        if(act == NULL || act->name == NULL)
                return "";
        /* else */
        return act->name;
}
