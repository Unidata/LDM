/*
 *   Copyright 1994, University Corporation for Atmospheric Research
 *   See top level COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: pqcreate.c,v 1.13.16.1.2.2 2007/02/12 20:38:54 steve Exp $ */

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
        -v\n\
        -c\n\
        -f\n\
        -l logfname\n\
        -S nproducts\n\
       (default pqfname is \"%s\")\n\
"

        (void)fprintf(stderr, USAGE_FMT,
                        av0,
                        av0,
                        getQueuePath());
        exit(1);
}


int main(int ac, char *av[])
{
        const char *pqfname = getQueuePath();
        int pflags = PQ_NOCLOBBER;
        off_t initialsz = 0;
        size_t nproducts = 0;
        pqueue *pq = NULL;
        int errnum = 0;

        /*
         * initialize logger
         */
        (void)log_init(av[0]);

        int ch;
        char *qopt = NULL;
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
                        qopt = optarg;
                        break;
                case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        break;
                case 'l':
                        (void)log_set_destination(optarg);
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
                if(qopt)        
                        usage(av[0]);
                qopt =  av[ac - 1];
        }

        if(qopt)
                pqfname = qopt ;

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
#define PQ_AVG_PRODUCT_SIZE 51000 // approximate mean size on 2014-08-21
                /* For default number of product slots, use average product size estimate */
                nproducts = initialsz/PQ_AVG_PRODUCT_SIZE;
        }


        log_info("Creating %s, %ld bytes, %ld products.\n",
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
