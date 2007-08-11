/* xzgv v0.1 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * readmrf.c - read the 1-bit mono `mrf' format (based on zgv's mrf reader).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "reader.h"
#include "readmrf.h"


static int bitbox,bitsleft;



void bit_init()
{
bitbox=0; bitsleft=0;
}


int bit_input(FILE *in)
{
if(bitsleft==0)
  {
  bitbox=fgetc(in);
  bitsleft=8;
  }

bitsleft--;
return((bitbox&(1<<bitsleft))?1:0);
}


void do_square(FILE *in,unsigned char *image,int ox,int oy,int w,int size)
{
int x,y,c;

/* is it all black or white? */

if(size==1 || bit_input(in))
  {
  /* yes, next bit says which. */
  c=bit_input(in);
  for(y=0;y<size;y++)
    for(x=0;x<size;x++)
      image[(oy+y)*w+ox+x]=c?255:0;
  }
else
  {
  /* not all one colour, so recurse. */
  size>>=1;
  do_square(in,image,ox,oy,w,size);
  do_square(in,image,ox+size,oy,w,size);
  do_square(in,image,ox,oy+size,w,size);
  do_square(in,image,ox+size,oy+size,w,size);
  }
}



/* returns 24-bit image in bmap */
int read_mrf_file(char *filename,unsigned char **bmap,int *wp,int *hp)
{
FILE *in;
int w,h,w64,h64,x,y,val;
unsigned char *image;
char buf[13];
int totalsq;

*bmap=NULL;

if((in=fopen(filename,"rb"))==NULL)
  return(0);

fread(buf,1,13,in);

if(strncmp(buf,"MRF1",4)!=0)
  return(0);

if(buf[12]!=0)
  return(0);

w=((buf[4]<<24)|(buf[5]<<16)|(buf[6]<<8)|buf[7]);
h=((buf[8]<<24)|(buf[9]<<16)|(buf[10]<<8)|buf[11]);

if(w==0 || h==0)
  return(0);

/* w64 is units-of-64-bits width, h64 same for height */
w64=(w+63)/64;
h64=(h+63)/64;

if(WH_BAD(w64*64,h64*64) || WH_BAD(w,h) ||
   (*bmap=malloc(w*h*3))==NULL ||
   (image=calloc(w64*h64*64*64,1))==NULL)
  {
  if(*bmap) free(*bmap),*bmap=NULL;
  return(0);
  }

/* now recursively input squares. */

/* init bit input */
bit_init();

totalsq=w64*h64;

for(y=0;y<h64;y++)
  for(x=0;x<w64;x++)
    do_square(in,image,x*64,y*64,w64*64,64);

fclose(in);

/* write real image */
for(y=0;y<h;y++)
  for(x=0;x<w;x++)
    {
    val=image[y*w64*64+x];
    (*bmap)[(y*w+x)*3  ]=val;
    (*bmap)[(y*w+x)*3+1]=val;
    (*bmap)[(y*w+x)*3+2]=val;
    }

free(image);

*wp=w; *hp=h;

return(1);
}
