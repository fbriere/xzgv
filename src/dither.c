/* xzgv v0.1 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * dither.c - dithering routine.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dither.h"


static int *evenerr=NULL,*odderr=NULL;
static unsigned char *dbuf=NULL;


int ditherinit(int w)
{
ditherfinish();		/* make sure any previous mem is unallocated */
if((evenerr=calloc(3*(w+10),sizeof(int)))==NULL ||
   (odderr =calloc(3*(w+10),sizeof(int)))==NULL ||
   (dbuf   =malloc(w))==NULL)
  return(0);
else
  return(1);
}


void ditherfinish()
{
if(evenerr!=NULL) { free(evenerr); evenerr=NULL; }
if(odderr!=NULL)  { free(odderr);  odderr=NULL; }
if(dbuf!=NULL)    { free(dbuf);    dbuf=NULL; }
}


void ditherline(unsigned char *theline,int linenum,int width)
{
int x,y,lx;
int c0,c1,c2,times2;
int terr0,terr1,terr2,actual0,actual1,actual2;
int start,addon,r,g,b;
int *thiserr;
int *nexterr;

y=linenum;
if((y&1)==0)
  {start=0; addon=1;
  thiserr=evenerr+3; nexterr=odderr+width*3;}
else
  {start=width-1; addon=-1;
  thiserr=odderr+3; nexterr=evenerr+width*3;}
nexterr[0]=nexterr[1]=nexterr[2]=0;
x=start;
for(lx=0;lx<width;lx++)
  {
  r=theline[x*3];
  g=theline[x*3+1];
  b=theline[x*3+2];

  terr0=r+((thiserr[0]+8)>>4);
  terr1=g+((thiserr[1]+8)>>4);
  terr2=b+((thiserr[2]+8)>>4);

  /* is this going to screw up on white? */  
  actual0=(terr0>>5)*255/7;
  actual1=(terr1>>5)*255/7;
  actual2=(terr2>>6)*255/3;
  
  if(actual0<0) actual0=0; if(actual0>255) actual0=255;
  if(actual1<0) actual1=0; if(actual1>255) actual1=255;
  if(actual2<0) actual2=0; if(actual2>255) actual2=255;
  
  c0=terr0-actual0;
  c1=terr1-actual1;
  c2=terr2-actual2;

  times2=(c0<<1);
  nexterr[-3] =c0; c0+=times2;
  nexterr[ 3]+=c0; c0+=times2;
  nexterr[ 0]+=c0; c0+=times2;
  thiserr[ 3]+=c0;

  times2=(c1<<1);
  nexterr[-2] =c1; c1+=times2;
  nexterr[ 4]+=c1; c1+=times2;
  nexterr[ 1]+=c1; c1+=times2;
  thiserr[ 4]+=c1;

  times2=(c2<<1);
  nexterr[-1] =c2; c2+=times2;
  nexterr[ 5]+=c2; c2+=times2;
  nexterr[ 2]+=c2; c2+=times2;
  thiserr[ 5]+=c2;

  dbuf[x]=(actual0>>5)*32+(actual1>>5)*4+(actual2>>6);

  thiserr+=3;
  nexterr-=3;
  x+=addon;
  }

memcpy(theline,dbuf,width);
}
