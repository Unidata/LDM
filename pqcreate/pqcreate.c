/**
 * Creats an LDM product-queue.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/*
 * Create an empty ldm product queue of a given size
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "ldm.h"
#include "globals.h"
#include "log.h"
#include "pq.h"


static void
usage(const char *av0)
{
#define USAGE_FMT "\
Usage: %s [options] <initialsz>[k|m|g] <pqfname>\n\
       %s [options] -s <initialsz>[k|m|g] [-q <pqfname>]\n\
Options:\n\
        -v           Verbose logging\n\
        -c           Clobber existing product-queue if it exists\n\
        -f           Fast creation. Won't fill-in file blocks.\n\
        -l dest      Log to `dest`. One of: \"\" (system logging daemon),\n\
                     \"-\" (standard error), or file `dest`. Default is\n\
                     \"%s\"\n\
        -S nproducts Maximum number of product to hold\n\
        -s byteSize  Maximum number of bytes to hold\n\
       (default pqfname is \"%s\")\n\
"

        (void)fprintf(stderr, USAGE_FMT,
                        av0,
                        av0,
                        log_get_default_destination(),
                        getDefaultQueuePath());
        exit(1);
}


int main(int ac, char *av[])
{
        int pflags = PQ_NOCLOBBER;
        off_t initialsz = 0;
        size_t nproducts = 0;
        pqueue *pq = NULL;
        int errnum = 0;

        /*
         * initialize logger
         */
        if (log_init(av[0])) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }

        int ch;
        bool qOptUsed = false;
        char *sopt = NULL;
        char *Sopt = NULL;
        extern char     *optarg;
        extern int       optind;

        while ((ch = getopt(ac, av, "xvcfq:s:S:l:")) != EOF)
                switch (ch) {
                case 'v':
                        if (!log_is_enabled_info)
                            (void)log_set_level(LOG_LEVEL_INFO);
                        break;
                case 'c':
                        pflags &= ~PQ_NOCLOBBER;
                        break;
                case 'f':
                        pflags |= PQ_SPARSE;
                        break;
                case 's':
                        sopt = optarg;
                        break;
                case 'S':
                        Sopt = optarg;
                        break;
                case 'q':
                        setQueuePath(optarg);
                        qOptUsed = true;
                        break;
                case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        break;
                case 'l':
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            usage(av[0]);
                        }
                        break;
                case '?':
                        usage(av[0]);
                        break;
                }
        
        if(ac - optind > 1)
        {
                if(sopt)
                        usage(av[0]);
                sopt = av[ac - 2];
        }
        if(ac - optind > 0)
        {
                if(qOptUsed)
                        usage(av[0]);
                setQueuePath(av[ac-1]);
        }

        const char* const pqfname = getQueuePath();

        if (sopt) {
            char*       cp;
            int         exponent = 0;

            errno = 0;
            initialsz = strtol(sopt, &cp, 0);

            if (0 != errno) {
                initialsz = 0; /* trigger error below */
            }
            else {
                switch (*cp) {
                    case 0:
                        break;
                    case 'k':
                    case 'K':
                        exponent = 1;
                        break;
                    case 'm':
                    case 'M':
                        exponent = 2;
                        break;
                    case 'g':
                    case 'G':
                        exponent = 3;
                        break;
                    default:
                        initialsz = 0; /* trigger error below */
                        break;
                }

                if (0 < initialsz) {
                    int     i;

                    for (i = 0; i < exponent; i++) {
                        initialsz *= 1000;
                        
                        if (0 >= initialsz) {
                            fprintf(stderr, "Size \"%s\" too big\n", sopt);
                            usage(av[0]);
                        }
                    }
                }
            }
        }
        if(initialsz <= 0)
        {
                if(sopt)
                        fprintf(stderr, "Illegal size \"%s\"\n", sopt);
                else
                        fprintf(stderr, "No size specified\n");
                usage(av[0]);
        }

        if(Sopt != NULL)
        {
                nproducts = (size_t)atol(Sopt);
                if(nproducts == 0)
                {
                        fprintf(stderr, "Illegal nproducts \"%s\"\n", Sopt);
                }
        }
        else
        {
// Approximate mean size, for all feeds, on 2021-04-15
#define PQ_AVG_PRODUCT_SIZE 140000
                /* For default number of product slots, use average product size estimate */
                nproducts = initialsz/PQ_AVG_PRODUCT_SIZE;
        }


        log_info_q("Creating %s, %ld bytes, %ld products.\n",
                pqfname, (long)initialsz, (long)nproducts);

        errnum = pq_create(pqfname, 0666, pflags,
                0, initialsz, nproducts, &pq);
        if(errnum)
        {
                fprintf(stderr, "%s: create \"%s\" failed: %s\n",
                        av[0], pqfname, strerror(errnum));
                exit(1);
        }

        (void)pq_close(pq);

        return(0);
}
