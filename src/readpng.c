/* xzgv 0.6 - picture viewer for X, with file selector.
 * Copyright (C) 2000 Russell Marks. See main.c for license details.
 *
 * readpng.c - interface to pnglib, derived from both their example.c
 *              and (zgv's) readjpeg.c. Based on zgv 5.2's PNG reader.
 *
 * I was originally going to use the new png_read_png(), which would
 * be ideal for xzgv and make the PNG reader trivial, but you need
 * libpng 1.0.6 for that. And (among other considerations) Debian 2.2
 * only has 1.0.5. :-/
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <png.h>
#include <setjmp.h>	/* after png.h to avoid horrible thing in pngconf.h */
#include "reader.h"
#include "readpng.h"


/* must be global to allow aborting in mid-read */
static png_structp png_ptr;
static png_infop info_ptr;



void my_png_error(png_structp png_ptr,const char *message)
{
/* XXX currently does nothing with the error message */

/* cleanup is done after jump back, so just do that now... */
longjmp(png_ptr->jmpbuf,1);
}


/* No warning messages */
void my_png_warning(png_structp png_ptr,const char *message)
{
}


int read_png_file(char *filename,unsigned char **theimageptr,int *wp,int *hp)
{
static FILE *in;
static unsigned char palette[256*3];
unsigned char *rowptr,*theimage;
png_uint_32 uw,uh;
int width,height;
int f,y;
int bitdepth,colourtype,interlacetype;
int ilheight,number_passes;
png_color_16 my_background={0,0,0,0,0};

*theimageptr=NULL;

if((in=fopen(filename,"rb"))==NULL)
  return(0);

png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,
	my_png_error,my_png_warning);
if(png_ptr==NULL)
  {
  fclose(in);
  return(0);
  }

if((info_ptr=png_create_info_struct(png_ptr))==NULL)
  {
  fclose(in);
  png_destroy_read_struct(&png_ptr,NULL,NULL);
  return(0);
  }

if(setjmp(png_ptr->jmpbuf))
  {
  /* if we get here, there was an error. */
  /* don't use local variables here, they may have been blasted */
  png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
  fclose(in);
  /* XXX make it use error message somehow? */
  return(0);
  }

png_init_io(png_ptr,in);
png_read_info(png_ptr,info_ptr);

/* the uw/uh stuff is to avoid a `pointer mismatch' compiler warning */
png_get_IHDR(png_ptr,info_ptr,&uw,&uh,&bitdepth,
	&colourtype,&interlacetype,(int *)NULL,(int *)NULL);
*wp=width=uw; *hp=height=uh;

/* now lots and lots of config stuff... */

if(bitdepth==16)
  png_set_strip_16(png_ptr);

png_set_packing(png_ptr);

if(bitdepth!=1)		/* changing background when 1-bit may cause problems */
  png_set_background(png_ptr,&my_background,1.0,0,1.0);

if(colourtype==PNG_COLOR_TYPE_GRAY && bitdepth<8)
  png_set_expand(png_ptr);

if(colourtype==PNG_COLOR_TYPE_GRAY ||
   colourtype==PNG_COLOR_TYPE_GRAY_ALPHA)
  png_set_gray_to_rgb(png_ptr);

/* XXX should do gamma stuff */

number_passes=png_set_interlace_handling(png_ptr);

/* fix palette (probably not needed now, but will be if I do gamma later) */
png_read_update_info(png_ptr,info_ptr);

if(colourtype==PNG_COLOR_TYPE_PALETTE)
  {
  png_colorp cols;
  int palsiz;
  
  png_get_PLTE(png_ptr,info_ptr,&cols,&palsiz);
  for(f=0;f<palsiz;f++)
    {
    palette[    f]=cols[f].red;
    palette[256+f]=cols[f].green;
    palette[512+f]=cols[f].blue;
    }
  }

/* allocate image memory */
if(WH_BAD(width,height) ||
   (*theimageptr=theimage=malloc(width*height*3))==NULL)
  {
  png_read_end(png_ptr,info_ptr);
  png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
  fclose(in);
  return(0);
  }

ilheight=height*number_passes;

/* read the image */
for(y=0;y<ilheight;y++)
  {
  rowptr=theimage+(y%height)*width*(colourtype==PNG_COLOR_TYPE_PALETTE?1:3);
  png_read_rows(png_ptr,&rowptr,NULL,1);
  }

/* expand a palette-based one into RGB */
if(colourtype==PNG_COLOR_TYPE_PALETTE)
  {
  unsigned char *src,*dst;
  int numpix=height*width;

  src=theimage+numpix;
  dst=theimage+numpix*3;

  for(f=0;f<numpix;f++)
    {
    *--dst=palette[512+*--src];
    *--dst=palette[256+*src];
    *--dst=palette[*src];
    }
  }

png_read_end(png_ptr,info_ptr);
png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
fclose(in);

return(1);
}
