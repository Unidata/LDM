/**
 * @mainpage LDM Logging
 *
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
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
 * two implementations: one using the `Log4C` library and the other using the
 * original `ulog` module that came with the LDM (that module is still part of
 * the LDM library for backward compatibility with user-developed code). By
 * default the `Log4C` implementation is used. The `ulog` implementation will be
 * used if the option `--with-ulog` is given to the `configure` script.
 *
 * @section example Example
 * Here's a contrived example:
 *
 * @code{.c}
 *     #include <mylog.h>
 *     #include <errno.h>
 *     #include <unistd.h>
 *
 *     static int system_failure()
 *     {
 *         (void)close(-1); // Guaranteed failure
 *         mylog_add_syserr("close() failure"); // Uses `errno`; adds to list
 *         return -1;
 *     }
 *
 *     static int func()
 *     {
 *         int status = system_failure();
 *         if (status)
 *             mylog_add("system_failure() returned %d", status); // adds to list
 *         return status;
 *     }
 *
 *     int main(int ac, char* av)
 *     {
 *         ...
 *         mylog_init(av[0]); // Necessary
 *         ...
 *         while ((int c = getopt(ac, av, "l:vx") != EOF) {
 *             extern char *optarg;
 *             switch (c) {
 *                 case 'l':
 *                      (void)mylog_set_output(optarg);
 *                      break;
 *                 case 'v':
 *                      (void)mylog_set_level(MYLOG_LEVEL_INFO);
 *                      break;
 *                 case 'x':
 *                      (void)mylog_set_level(MYLOG_LEVEL_DEBUG);
 *                      break;
 *                 ...
 *             }
 *         }
 *         ...
 *         if (func()) {
 *             if (mylog_is_enabled_info)
 *                 // Adds to list, prints list at INFO level, and clears list
 *                 mylog_info("func() failure: reason = %s", expensive_func());
 *         }
 *         if (func()) {
 *             // Adds to list, prints list at ERROR level, and clears list
 *             mylog_error("func() failure: reason = %s", cheap_func());
 *         }
 *         ...
 *         (void)mylog_fini(); // Good form
 *         return 0;
 *     }
 * @endcode
 */
