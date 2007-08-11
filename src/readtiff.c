/* xzgv 0.7 - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks. See main.c for license details.
 *
 * readtiff.c - read TIFF files.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/file.h>  /* for open et al */
#include <tiffio.h>
#include "reader.h"
#include "readtiff.h"


int read_tiff_file(char *filename,unsigned char **imagep,int *wp,int *hp)
{
TIFF *in;
unsigned char *src,*dst,*ptr;
int width,height;
int f,numpix,w3;
unsigned char *image;

TIFFSetErrorHandler(NULL);	/* no error messages */
TIFFSetWarningHandler(NULL);	/* no warning messages either */

if((in=TIFFOpen(filename,"r"))==NULL)
  return(0);

TIFFGetField(in,TIFFTAG_IMAGEWIDTH,&width);
TIFFGetField(in,TIFFTAG_IMAGELENGTH,&height);

/* the width*3 guarantees there'll be at least one line
 * spare for the flip afterwards.
 */
numpix=width*height;
if(WH_BAD(width,height) ||
   (image=malloc(numpix*sizeof(uint32)+width*3))==NULL)
  {
  TIFFClose(in);
  return(0);
  }

if(!TIFFReadRGBAImage(in,width,height,(uint32 *)image,0))
  {
  free(image);
  TIFFClose(in);
  return(0);
  }

TIFFClose(in);


/* This is a pretty crappy way to work :-), but the alternative
 * way (supplying routines to write any contiguous/planar RGBA chunks
 * you get passed) is, stunningly, even worse.
 */

/* RGBA to RGB */
src=image+sizeof(uint32);
dst=image+3;
for(f=1;f<numpix;f++)
  {
  *dst++=*src++;
  *dst++=*src++;
  *dst++=*src++;
  src++;
  }

/* flip the image vertically */
src=image;
w3=width*3;
dst=image+(height-1)*w3;
ptr=dst+w3;	/* use extra space for temp row */
for(f=0;f<height/2;f++)
  {
  memcpy(ptr,src,w3);
  memcpy(src,dst,w3);
  memcpy(dst,ptr,w3);
  src+=w3;
  dst-=w3;
  }

image=realloc(image,numpix*3);

/* If the realloc() fails (which it can't?), the memory will still be
 * there as before, so we can still return ok.
 */

*imagep=image;
*wp=width;
*hp=height;

return(1);
}
