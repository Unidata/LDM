/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libpng/png.h"
#include "md5.h"

int pngcount=0;
FILE *fp;
char *prodmap;
int prodoff;
int png_deflen;
MD5_CTX *md5p;

int PNG_write_calls=0;

png_structp png_ptr;
png_infop info_ptr;

int png_get_prodlen()
{
return(prodoff);
}

void png_header(char *memheap,int length)
{
/*printf("png_header %d [%d]\n",prodoff,length);*/
memcpy(prodmap+prodoff,memheap,length);
MD5Update(md5p, (unsigned char *)(prodmap+prodoff), length);
prodoff += length;
png_deflen += length;
}

void unidata_output_flush_fn(png_structp png_p)
{
printf("flush called %d\n",png_deflen);
}

void write_row_callback(png_structp pngp,png_uint_32 rownum,int pass)
{
/*printf("test callback %d %d [deflen %d]\n",rownum,pass,png_deflen);*/
}

void unidata_write_data_fn(png_structp png_p, png_bytep data, png_uint_32 length)
{
png_header((char *)data,(int)length);
}

void png_set_memheap(char *memheap,MD5_CTX *md5ctxp)
{
prodmap = memheap;
prodoff = 0;
png_deflen = 0;
md5p = md5ctxp;
}

void pngout_init(int width,int height)
{
char filename[80];
int bit_depth=8;
int color_type=PNG_COLOR_TYPE_GRAY;
int interlace_type = PNG_INTERLACE_NONE;
int compression_type = PNG_COMPRESSION_TYPE_DEFAULT;
int filter_type = PNG_FILTER_TYPE_DEFAULT;

/*sprintf(filename,"testpng_file_%d.png",pngcount);
pngcount++;
fp = fopen(filename,"wb");

printf("opening %s with geom %d %d\n",filename,width,height);*/

png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
          NULL, NULL, NULL);/*
          (png_voidp)user_error_ptr,user_error_fn,
          user_warning_fn);*/

if(!png_ptr) return;

info_ptr = png_create_info_struct(png_ptr);

if(!info_ptr)
   {
   png_destroy_write_struct(&png_ptr,(png_infopp)NULL);
   return;
   }

if(setjmp(png_ptr->jmpbuf))
   {
   png_destroy_write_struct(&png_ptr,&info_ptr);
   return;
   }

png_set_write_fn(png_ptr,info_ptr,(png_rw_ptr)unidata_write_data_fn,(png_flush_ptr)unidata_output_flush_fn);
/*png_init_io(png_ptr,fp);*/
png_set_write_status_fn(png_ptr,write_row_callback);

/*png_set_compression_level(png_ptr,Z_BEST_COMPRESSION);*/
png_set_IHDR(png_ptr,info_ptr,width,height,bit_depth,color_type,
             interlace_type,compression_type,filter_type);
png_write_info(png_ptr,info_ptr);
PNG_write_calls = 0;
}

void pngout_end()
{
/* try this, we get core dumps if try to shut down before any writes take place */
/*if(PNG_write_calls > 0) */
if ((png_ptr->mode & 0x04/* PNG_HAVE_IDAT */))
   png_write_end(png_ptr,NULL);


png_destroy_write_struct(&png_ptr,&info_ptr);
/*fclose(fp);*/
PNG_write_calls = 0;
}

void pngwrite(char *memheap)
{
png_write_row(png_ptr,(png_bytep)memheap);
PNG_write_calls++;

}
