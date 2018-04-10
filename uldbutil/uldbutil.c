/**
 * Copyright 2012 University Corporation for Atmospheric Research.
 * All rights reserved.
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This file contains the uldbutil(1) utility for accessing the upstream LDM
 * database.
 *
 * Created on: Aug 20, 2012
 * Author: Steven R. Emmerson
 */

#include <config.h>

#include <libgen.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

#include "ldm.h"
#include "log.h"
#include "uldb.h"
#include "inetutil.h"
#include "ldmprint.h"
#include "prod_class.h"

static void printUsage(
        const char* progname)
{
    log_add("Usages:");
    log_add("  Print Database:     %s", progname);
    log_add("  Delete Database:    %s -d", progname);
    log_flush_error();
}

/**
 * @retval 0    Success
 * @retval 1    Invocation error
 * @retval 2    The upstream LDM database doesn't exist
 * @retval 3    The upstream LDM database exists but couldn't be accessed
 */
int main(
        int argc,
        char* argv[])
{
    const char* const progname = basename(argv[0]);
    int               status = 0;
    int               delete = 0;

    (void)log_init(progname);

    {
        int ch;

        opterr = 0; /* Suppress getopt(3) error messages */

        while (0 == status && (ch = getopt(argc, argv, "d")) != -1) {
            switch (ch) {
            case 'd':
                delete = 1;
                break;
            default:
                log_add("Unknown option: %c", optopt);
                printUsage(progname);
                status = 1;
                break;
            }
        }

        if (0 == status && optind < argc) {
            log_add("Too many arguments");
            printUsage(progname);
            status = 1;
        }
    }

    if (0 == status) {
        if (delete) {
            status = uldb_delete(NULL);
            if (status) {
                if (ULDB_EXIST == status) {
                    log_info_q("The upstream LDM database doesn't exist");
                    status = 2;
                }
                else {
                    log_error_q("Couldn't open the upstream LDM database");
                    status = 3;
                }
            }
        }
        else {
            status = uldb_open(NULL);
            if (status) {
                if (ULDB_EXIST == status) {
                    log_add("The upstream LDM database doesn't exist");
                    log_notice_q("Is the LDM running?");
                    status = 2;
                }
                else {
                    log_error_q("Couldn't open the upstream LDM database");
                    status = 3;
                }
            }
            else {
                uldb_Iter* iter;

                status = uldb_getIterator(&iter);
                if (status) {
                    log_error_q("Couldn't get database iterator");
                    status = 3;
                }
                else {
                    const uldb_Entry* entry;

                    status = 0;

                    for (entry = uldb_iter_firstEntry(iter); NULL != entry;
                            entry = uldb_iter_nextEntry(iter)) {
                        prod_class* prodClass;

                        if (uldb_entry_getProdClass(entry, &prodClass)) {
                            log_error_q(
                                    "Couldn't get product-class of database entry");
                            status = 3;
                            break;
                        }
                        else {
                            const struct sockaddr_in* sockAddr =
                                    uldb_entry_getSockAddr(entry);
                            char buf[2048];
                            const char* const type =
                                    uldb_entry_isNotifier(entry) ?
                                            "notifier" : "feeder";

                            (void) s_prod_class(buf, sizeof(buf), prodClass);
                            (void) printf("%ld %d %s %s %s %s\n",
                                    (long) uldb_entry_getPid(entry),
                                    uldb_entry_getProtocolVersion(entry), type,
                                    hostbyaddr(sockAddr), buf,
                                    uldb_entry_isPrimary(entry)
                                            ? "primary" : "alternate");
                            free_prod_class(prodClass);
                        } /* "prodClass" allocated */
                    } /* entry loop */

                    uldb_iter_free(iter);
                } /* got database iterator */

                (void) uldb_close();
            } /* database opened */
        }
    } /* correct invocation syntax */

    exit(status);
}
