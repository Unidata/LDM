/**
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 *  ULOGGER -- command line interface to log(3)
 *
 *  This program reads messages from an input and logs them via the log(3)
 *  library.
 */

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h> /* getlogin() */
#include <string.h>
#include <strings.h>
#include "log.h"

typedef struct {
        const char* c_name;
        const int   c_val;
} code_t;

static const code_t log_levels[] = {
        {"fatal",        LOG_LEVEL_FATAL},
        {"debug",        LOG_LEVEL_DEBUG},
        {"err",          LOG_LEVEL_ERROR},
        {"error",        LOG_LEVEL_ERROR},   // DEPRECATED
        {"info",         LOG_LEVEL_INFO},
        {"notice",       LOG_LEVEL_NOTICE},
        {"warn",         LOG_LEVEL_WARNING}, // DEPRECATED
        {"warning",      LOG_LEVEL_WARNING},
        {NULL,           -1}};

static code_t facilitynames[] = {
        {"auth",         LOG_AUTH},
#ifdef LOG_AUTHPRIV
        {"authpriv",     LOG_AUTHPRIV},
#endif
#ifdef LOG_CRON
        {"cron",         LOG_CRON},
#endif
        {"daemon",       LOG_DAEMON},
        {"kern",         LOG_KERN},
        {"lpr",          LOG_LPR},
        {"mail",         LOG_MAIL},
#ifdef LOG_NEWS
        {"news",         LOG_NEWS},
#endif
        {"security",     LOG_AUTH},               // DEPRECATED}
        {"syslog",       LOG_SYSLOG},
        {"user",         LOG_USER},
#ifdef LOG_UUCP
        {"uucp",         LOG_UUCP},
#endif
        {"local0",       LOG_LOCAL0},
        {"local1",       LOG_LOCAL1},
        {"local2",       LOG_LOCAL2},
        {"local3",       LOG_LOCAL3},
        {"local4",       LOG_LOCAL4},
        {"local5",       LOG_LOCAL5},
        {"local6",       LOG_LOCAL6},
        {"local7",       LOG_LOCAL7},
        {NULL,           -1}
};

static void
usage(const char *av0)
{
        (void)fprintf(stderr,
            "%s: [-i] [-f file] [-p pri] [-t tag] [-l dest] [ message ... ]\n",
            av0);
        exit(1);
}


static int
decode(
        const char* restrict   name,
        const code_t* restrict codetab)
{
        const code_t *c;

        if (isdigit(*name))
                return (atoi(name));

        for (c = codetab; c->c_name; c++)
                if (!strcasecmp(name, c->c_name))
                        return (c->c_val);

        return (-1);
}


/*
 *  Decode a symbolic name to a numeric value
 */
static int
pencode( char *s, int* const restrict facility)
{
        char *save;
        int lev;

        for (save = s; *s && *s != '.'; ++s);
        if (*s) {
                *s = '\0';
                *facility = decode(save, facilitynames);
                if (*facility < 0) {
                        (void)fprintf(stderr,
                            "logger: unknown facility name: %s.\n", save);
                        exit(1);
                }
                *s++ = '.';
        }
        else {
                s = save;
        }
        lev = decode(s, log_levels);
        if (lev < 0) {
                (void)fprintf(stderr,
                    "logger: unknown priority name: %s.\n", save);
                exit(1);
        }
        return lev;
}


int main(int argc, char *argv[])
{
        extern char *optarg;
        extern int errno, optind;
        int pri = LOG_LEVEL_NOTICE;
        int facility = LOG_LDM;
        int ch, logflags = 0;
        char buf[1024];

        if (log_init(argv[0])) {
            fprintf(stderr, "log_init() failure\n");
            return 1;
        }

#ifdef LOG_PERROR
        while ((ch = getopt(argc, argv, "f:ip:st:l:")) != EOF)
#else
        while ((ch = getopt(argc, argv, "f:ip:t:l:")) != EOF)
#endif
                switch((char)ch) {
                case 'f':               /* file to log */
                        if (freopen(optarg, "r", stdin) == NULL) {
                                (void)fprintf(stderr, "logger: %s: %s.\n",
                                    optarg, strerror(errno));
                                exit(1);
                        }
                        break;
                case 'i':               /* log process id also */
                        logflags |= LOG_PID;
                        break;
                case 'p':               /* priority */
                        pri = pencode(optarg, &facility);
                        break;
#ifdef LOG_PERROR
                case 's':               /* log to standard error */
                        logflags |= LOG_PERROR;
                        break;
#endif
                case 't':               /* tag */
                        log_warning("Tag option is ignored");
                        break;
                case 'l':               /* logfname */
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            usage(argv[0]);
                        }
                        break;
                case '?':
                default:
                        usage(argv[0]);
                }
        argc -= optind;
        argv += optind;

        log_set_facility(facility);
        log_set_options(logflags);

        /* log input line if appropriate */
        if (argc > 0) {
                char *p, *endp;
                int len;

                for (p = buf, endp = buf + sizeof(buf) - 2; *argv;) {
                        len = strlen(*argv);
                        if (p + len > endp && p > buf) {
                                log_log_q(pri, "%s", buf);
                                p = buf;
                        }
                        if (len > sizeof(buf) - 1)
                                log_log_q(pri, "%s", *argv++);
                        else {
                                if (p != buf)
                                        *p++ = ' ';
                                memcpy(p, *argv++, len);
                                *(p += len) = '\0';
                        }
                }
                if (p != buf)
                        log_log_q(pri, "%s", buf);
                return(0);
        }

        /* main loop */
        while (fgets(buf, sizeof(buf), stdin) != NULL)
                log_log_q(pri, "%s", buf);

        return(0);
}
