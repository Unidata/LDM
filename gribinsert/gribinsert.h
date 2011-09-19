/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include "sys/types.h"
int get_grib_info( unsigned char *data, off_t filelen, off_t *off, size_t *len, int *gversion);
void get_gribname ( int gversion, char *data, size_t sz, char *filename, int seqno, char *ident);

const char *s_pds_center(unsigned char center, unsigned char subcenter);
const char *s_pds_model(unsigned char center, unsigned char model);


