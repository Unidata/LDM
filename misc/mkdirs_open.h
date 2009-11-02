/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */
#ifndef _MKDIRS_H_
#define _MKDIRS_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus) || defined(__STDC__)

/*
 * Like mkdir(2), but will create components as necessary.
 * The specified mode is used to create the directories.
 * Returns 0 if successful, -1 on failure.
 */
int
mkdirs(
    const char* const   path,
    const mode_t        mode);
/*
 * Like open(2), but will create components as necessary.
 * Returns valid file descriptor if successful, -1 on failure.
 */
int
mkdirs_open(
    const char*         path,
    int                 flags,
    mode_t              mode);
/*
 * Check to see if we have access to all components of 'path'
 * up to the last component. (Doesn't check the access of the full path)
 * If 'create' is no zero, attempt to create path components (directories)
 * as necessary.
 * Returns 0 if access is ok, -1 on error.
 */
int
diraccess(
    const char*         path,
    int                 access_m,
    int                 create);

#else /* Old Style C */

extern int mkdirs();
extern int mkdirs_open();
extern int diraccess();

#endif

#ifdef __cplusplus
}
#endif

#endif /* !_MKDIRS_H_ */
