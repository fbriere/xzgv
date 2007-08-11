/* xzgv 0.7 - picture viewer for X, with file selector.
 * Copyright (C) 1999-2001 Russell Marks. See main.c for license details.
 *
 * readprf.c - read PRF files, heavily based on Brian Raiter's `prftopnm'.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "reader.h"
#include "readprf.h"

#define squaresize	64

static int width,height,bits,planes;
static unsigned char *planebuf[4],*imagebuf;
static int fbitcount,fbyte;



static int bitsin(FILE *in,int nbits,unsigned char *val)
{
int bit;

*val=0;
for(bit=1<<(nbits-1);bit;bit>>=1)
  {
  if(fbitcount++&7)
    fbyte<<=1;
  else
    if((fbyte=fgetc(in))==EOF)
      return(0);
  if(fbyte&0x80)
    *val|=bit;
  }

return(1);
}

static int decodesquare(FILE *in,int ypos,int xpos,int size,int cb,unsigned char pixel)
{
static int const bitsize[]={0,1,2,2,3,3,3,3,4};
unsigned char *square;
unsigned char *sq;
unsigned char byte;
unsigned char count;
int y,n;

if(ypos>=height || xpos>=width)
  return(1);

square=imagebuf+(ypos%squaresize)*width+xpos;

if(size==1)
  {
  byte=0;
  if(cb)
    if(!bitsin(in,cb,&byte))
      return(0);
  *square=pixel|byte;
  return(1);
  }

if(!bitsin(in,bitsize[cb],&count))
  return(0);

cb-=count;
if(count) 
  {
  if(!bitsin(in,count,&byte))
    return(0);
  
  pixel|=byte<<cb;
  }

if(!cb) 
  {
  n=width-xpos;  if(n>size) n=size;
  y=height-ypos; if(y>size) y=size;
  
  for(sq=square;y;y--,sq+=width)
    memset(sq,pixel,n);
  
  return(1);
  }

n=size>>1;

return(decodesquare(in,ypos,xpos,n,cb,pixel)
       && decodesquare(in,ypos,xpos+n,n,cb,pixel)
       && decodesquare(in,ypos+n,xpos,n,cb,pixel)
       && decodesquare(in,ypos+n,xpos+n,n,cb,pixel));
}


int readprfrow(FILE *in,int ypos)
{
int x,z;

if(ypos>=height)
  return(0);

for(z=0;z<planes;z++)
  {
  imagebuf=planebuf[z];
  for(x=0;x<width;x+=squaresize)
    if(!decodesquare(in,ypos,x,squaresize,bits,0))
      return(-1);
  }

return(1);
}


int read_prf_file(char *filename,unsigned char **theimageptr,int *wp,int *hp)
{
FILE *in;
int f,n;
unsigned char buf[13],*src[4],*src8,*dst,v;
int bytepp,ypos;
int x,y;
int maxval;
int lookup[256];

fbitcount=0;
fbyte=0;

if((in=fopen(filename,"rb"))==NULL)
  return(0);

if(fread(buf,1,13,in)!=13 || strncmp(buf,"PRF1",4)!=0)
  {
  fclose(in);
  return(0);
  }

width=(buf[4]<<24)|(buf[5]<<16)|(buf[6]<<8)|buf[7];
height=(buf[8]<<24)|(buf[9]<<16)|(buf[10]<<8)|buf[11];
bits=(buf[12]&31)+1;
planes=((buf[12]>>5)&7)+1;

maxval=(1<<bits)-1;

/* XXX should it try to read those with more bits? */
if(width<=0 || height<=0 || bits>8)
  {
  fclose(in);
  return(0);
  }

/* we support grey, RGB, and RGBA (but with alpha ignored) */
if(planes!=1 && planes!=3 && planes!=4)
  {
  fclose(in);
  return(0);
  }

for(f=0;f<=maxval;f++)
  lookup[f]=(f*255)/maxval;
for(;f<256;f++)
  lookup[f]=0;

bytepp=3;
if(planes==1)
  bytepp=1;

n=width*squaresize;
if(WH_BAD(width,height) || (planebuf[0]=calloc(n,planes))==NULL)
  {
  fclose(in);
  return(0);
  }

for(f=1;f<planes;f++)
  planebuf[f]=planebuf[f-1]+n;

/* width/height already checked above */
if((*theimageptr=malloc(width*height*3))==NULL)
  {
  free(planebuf[0]);
  fclose(in);
  return(0);
  }

ypos=0;

while((f=readprfrow(in,ypos)))
  {
  if(f<0)
    {
    free(planebuf[0]);
    free(*theimageptr);
    fclose(in);
    return(0);
    }

  switch(planes)
    {
    case 1:
      for(y=0;y<squaresize && ypos+y<height;y++)
        {
        dst=*theimageptr+(ypos+y)*width*3;
        src8=planebuf[0]+y*width;

        for(x=0;x<width;x++)
          {
          v=lookup[*src8++];
          *dst++=v;
          *dst++=v;
          *dst++=v;
          }
        }
      break;
      
    case 3: case 4:
      /* more complicated - need to turn separate R,G,B planes into
       * interleaved RGB. And then, possibly fix depth.
       */
      for(y=0;y<squaresize && ypos+y<height;y++)
        {
        for(x=0;x<3;x++)
          src[x]=planebuf[x]+y*width;
        
        dst=*theimageptr+(ypos+y)*width*bytepp;

        for(x=0;x<width;x++)
          {
          *dst++=*src[0]++;
          *dst++=*src[1]++;
          *dst++=*src[2]++;
          }

        /* fix bit-depth if bits<8 */
        if(bits<8)
          {
          for(x=0;x<width*3;x++)
            {
            --dst;
            *dst=lookup[*dst];
            }
          }
        }
      
      break;
    }

  ypos+=squaresize;
  }

free(planebuf[0]);
fclose(in);

*wp=width;
*hp=height;

return(1);
}
