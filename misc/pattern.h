#ifndef PATTERN_H
#define PATTERN_H

#include "error.h"

typedef struct Pattern Pattern;

#ifdef __cplusplus
extern "C" {
#endif

ErrorObj*
pat_new(
    Pattern** const             pat,
    const char* const           ere,
    const int                   ignoreCase);

ErrorObj*
pat_clone(
    Pattern** const             dst,
    const Pattern* const        src);

int
pat_isMatch(
    const Pattern* const        pat,
    const char* const           string);

const char*
pat_getEre(
    const Pattern* const        pat);

void
pat_free(
    Pattern* const              pat);

#ifdef __cplusplus
}
#endif

#endif
