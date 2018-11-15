/**
 * This file declares a module that enables and disables root privileges.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 */

#ifndef _PRIV_H_
#define _PRIV_H_

/**
 * Ensures that the process may dump core on a Linux system.
 */
extern void
ensureDumpable();

/**
 * Enable root privileges if possible.
 */
extern void
rootpriv(void);

/**
 * Disable root privileges if possible.
 */
extern void
unpriv(void);

/**
 * Permanently disable root privileges if possible.
 */
extern void
endpriv(void);

#endif /* !_PRIV_H_ */
