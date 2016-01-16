/**
 * @mainpage LDM Logging
 *
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @author: Steven R. Emmerson
 *
 * @section contents Table of Contents
 * - \ref introduction
 * - \ref example
 *
 * @section introduction Introduction
 * This module is the logging system for the LDM. It comprises a single API with
 * two implementations: one using a simple implementation and the other using
 * the original `ulog` module that came with the LDM (that module is still part
 * of the LDM library for backward compatibility with user-developed code). By
 * default the simple implementation is used. The `ulog` implementation will be
 * used if the option `--with-ulog` is given to the `configure` script.
 *
 * @section example Example
 * Here's a contrived example:
 *
 * @code{.c}
 *     #include <log.h>
 *     #include <errno.h>
 *     #include <unistd.h>
 *
 *     static int system_failure()
 *     {
 *         (void)close(-1); // Guaranteed failure
 *         log_add_syserr("close() failure"); // Uses `errno`; adds to list
 *         return -1;
 *     }
 *
 *     static int func()
 *     {
 *         int status = system_failure();
 *         if (status)
 *             log_add("system_failure() returned %d", status); // adds to list
 *         return status;
 *     }
 *
 *     int main(int ac, char* av)
 *     {
 *         ...
 *         log_init(av[0]); // Necessary
 *         ...
 *         while ((int c = getopt(ac, av, "l:vx") != EOF) {
 *             extern char *optarg;
 *             switch (c) {
 *                 case 'l':
 *                      (void)log_set_output(optarg);
 *                      break;
 *                 case 'v':
 *                      (void)log_set_level(LOG_LEVEL_INFO);
 *                      break;
 *                 case 'x':
 *                      (void)log_set_level(LOG_LEVEL_DEBUG);
 *                      break;
 *                 ...
 *             }
 *         }
 *         ...
 *         if (func()) {
 *             if (log_is_enabled_info)
 *                 // Adds to list, prints list at INFO level, and clears list
 *                 log_info("func() failure: reason = %s", expensive_func());
 *         }
 *         if (func()) {
 *             // Adds to list, prints list at ERROR level, and clears list
 *             log_error("func() failure: reason = %s", cheap_func());
 *         }
 *         ...
 *         (void)log_fini(); // Good form
 *         return 0;
 *     }
 * @endcode
 */
