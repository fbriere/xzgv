/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * resizepic.c - resize pic with smoothing (for thumbnails).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>

#include "resizepic.h"


#define TN_CRUNCH_WIDTH		320
#define TN_CRUNCH_HEIGHT	240



/* used to resize (without smoothing) a picture loaded for
 * thumbnail generation into a (hopefully) smaller image,
 * which should then be reduced more nicely for the thumbnail.
 */
unsigned char *nasty_resizepic(unsigned char *theimage,
                               int width,int height,int *sw_ask,int *sh_ask)
{
int x,y,yp,yw,sw,sh,lastyp,scrnwide,scrnhigh;
unsigned char *rline,*outimage;

scrnwide=*sw_ask;
scrnhigh=*sh_ask;

/* try landscapey */
sw=scrnwide; sh=(scrnwide*height)/width;
if(sh>scrnhigh)
  /* no, oh well portraity then */
  sh=scrnhigh,sw=(scrnhigh*width)/height;

/* fix things for very thin images */
if(sh==0) sh++;
if(sw==0) sw++;

*sw_ask=sw; *sh_ask=sh;

/* so now our zoomed image will be sw x sh */

if(width<=sw) return(NULL);	/* abort if it's not smaller */

/* extra line added to avoid segfaults from obiwan errors in dest
 * of memcpy() below. (I don't such errors are actually possible,
 * but it can't hurt anyway.)
 */
if((outimage=malloc(sw*(sh+1)*3))==NULL)
  return(NULL);

lastyp=-1;
for(y=0;y<height;y++)
  {
  yp=(y*sh)/height;
  rline=outimage+yp*sw*3;
  if(yp!=lastyp)
    {
    yw=y*width;
    for(x=0;x<width;x++,yw++)
      memcpy(rline+(x*sw)/width*3,theimage+yw*3,3);
    lastyp=yp;
    }
  }

return(outimage);
}



/* resamples from 'theimage' to a new malloc'ed area, a pointer to which is
 * the return value. 
 * width x height	- size of source image
 * sw_ask x sh_ask	- requested size of dest. image
 * input and output image both 24-bit.
 * new width x height in put back in sw_ask and sh_ask
 */
unsigned char *resizepic(unsigned char *theimage,
                         int width,int height,int *sw_ask,int *sh_ask,
                         int allow_crunch)
{
int a,b,x,y,yp,yw,sw,sh,lastyp;
int c,pixwide,pixhigh;
int scrnwide,scrnhigh;
unsigned char *rline;
unsigned char *crunched=NULL;
int tmp2,tr,tg,tb,tn;
int xypos;

/* first, see if it would benefit from being reduced without
 * smoothing (which is faster) before the `nice' resizing.
 */
if(allow_crunch && (width>TN_CRUNCH_WIDTH || height>TN_CRUNCH_HEIGHT))
  {
  int our_sw_ask=TN_CRUNCH_WIDTH,our_sh_ask=TN_CRUNCH_HEIGHT;
  
  crunched=nasty_resizepic(theimage,width,height,&our_sw_ask,&our_sh_ask);
  
  /* if it worked, replace orig w/h with crunched pic's one, and
   * set theimage to point to it.
   */
  if(crunched)
    {
    width=our_sw_ask;
    height=our_sh_ask;
    theimage=crunched;
    }
  }

scrnwide=*sw_ask;
scrnhigh=*sh_ask;

if((rline=calloc(scrnwide*scrnhigh*3,1))==NULL) return(0);

/* try landscapey */
sw=scrnwide; sh=(int)((scrnwide*((long)height))/((long)width));
if(sh>scrnhigh)
  /* no, oh well portraity then */
  { sh=scrnhigh; sw=(int)((scrnhigh*((long)width))/((long)height)); }

/* fix things for very thin images */
if(sh==0) sh++;
if(sw==0) sw++;

*sw_ask=sw; *sh_ask=sh;

/* so now our zoomed image will be sw x sh */
if(width>sw || height>sh)
  /* it's been reduced - easy, just make 'em fit in less space */
  {
  lastyp=-1;
  pixhigh=(int)(((float)height)/((float)sh)+0.5);
  pixwide=(int)(((float)width)/((float)sw)+0.5);
  pixhigh++;
  pixwide++;
  for(y=0;y<height;y++)
    {
    yp=(y*sh)/height;
    if(yp!=lastyp)
      {
      yw=y*width;
      /* we try to resample it a bit */
      for(x=0;x<width;x++,yw++)
        {
        tr=tg=tb=tn=0;
        for(b=0;(b<pixhigh)&&(y+b<height);b++)
          for(a=0;(a<pixwide)&&(x+a<width);a++)
            {
            tmp2=(yw+a+b*width)*3;
            tr+=theimage[tmp2  ];
            tg+=theimage[tmp2+1];
            tb+=theimage[tmp2+2];
            tn++;
            }
        tr/=tn; tg/=tn; tb/=tn;
        xypos=3*(((x*sw)/width)+yp*scrnwide);
        rline[xypos]=tr;
        rline[xypos+1]=tg;
        rline[xypos+2]=tb;
        }
      lastyp=yp;
      }
    }
  }
else
  /* we just leave it the same size */
  {
  *sw_ask=width; *sh_ask=height;
  for(y=0;y<height;y++)
    for(x=0;x<width;x++)
      {
      c=(y*width+x)*3;
      rline[3*(y*scrnwide+x)  ]=theimage[c  ];
      rline[3*(y*scrnwide+x)+1]=theimage[c+1];
      rline[3*(y*scrnwide+x)+2]=theimage[c+2];
      }
  }

if(crunched) free(crunched);
  
return(rline);
}
