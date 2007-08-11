/* xzgv v0.1 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * readgif.c - read GIF files (based on zgv 3.3's GIF reader).
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "reader.h"
#include "readgif.h"


static unsigned char *image;
static unsigned char palette[768];

static int imagex,imagey,stoprightnow;
static int dc_cc,dc_eoi;                  /* the CC and EOI codes */
static int passnum,passyloc,passstep;     /* for interlaced GIFs */
static int interlaced,width,height,bpp,numcols;

static int pwr2[16]={1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,
                     16384,32768};


/* now this is for the string table.
 * the st_ptr array stores which pos to back reference to,
 *  each string is [...]+ end char, [...] is traced back through
 *  the 'pointer' (index really), then back through the next, etc.
 *  a 'null pointer' is = to UNUSED.
 * the st_chr array gives the end char for each.
 *  an unoccupied slot is = to UNUSED.
 */
#define UNUSED 32767
#define MAXSTR 4096
static int st_ptr[MAXSTR],st_chr[MAXSTR],st_last;
static int st_ptr1st[MAXSTR];

/* this is for the byte -> bits mangler:
 *  dc_bitbox holds the bits, dc_bitsleft is number of bits left in dc_bitbox,
 *  blocksize is how many bytes of an image sub-block we have left.
 */
static int dc_bitbox,dc_bitsleft,blocksize;

static int global_colour_map,local_colour_map;

struct
  {
  char sig[6];
  unsigned char wide_lo,wide_hi;
  unsigned char high_lo,high_hi;  /* these are 'screen size', BTW */
  unsigned char misc;             /* misc, and bpp */
  unsigned char back;             /* background index */
  unsigned char zero;             /* if this ain't zero, problem */
  } gifhed;
  
struct
  {
  unsigned char left_lo,left_hi;
  unsigned char top_lo,top_hi;
  unsigned char wide_lo,wide_hi;  /* `image size', often the same as screen */
  unsigned char high_lo,high_hi;
  unsigned char misc;
  } imagehed;


/* prototypes */
int readgifhed(FILE *in);
void readcolmap(FILE *in);
int readimagehed(FILE *in);
int decompress(FILE *in);
void inittable(int orgcsize);
int addstring(int oldcode, int chr);
int readcode(int *newcode, int numbits, FILE *in);
void outputstring(int code);
void outputchr(int code);
int findfirstchr(int code);


int read_gif_file(char *giffn,unsigned char **theimageptr,int *wp,int *hp)
{
FILE *in;

imagex=imagey=stoprightnow=dc_bitbox=dc_bitsleft=blocksize=0;

if((in=fopen(giffn,"rb"))==NULL)
  return(0);
else
  {
  if(!readgifhed(in))
    {
    fclose(in);
    return(0);
    }
  
  if(global_colour_map) readcolmap(in);
               
  if(!readimagehed(in))
    {
    fclose(in);
    return(0);
    }
  
  if(local_colour_map) readcolmap(in);
  
  if(WH_BAD(width,height) || (image=malloc(width*height*3))==NULL)
    {
    fclose(in);
    return(0);
    }
  
  if(!decompress(in))
    {
    fclose(in);
    free(image);
    return(0);
    }
  
  fclose(in);
  *theimageptr=image;
  *wp=width;
  *hp=height;

  return(1);
  }
}


int readgifhed(FILE *in)
{
fread(&gifhed,sizeof(gifhed),1,in);
if(strncmp(gifhed.sig,"GIF",3)!=0)
  return(0);
global_colour_map=(gifhed.misc&128)?1:0;
local_colour_map=0;
bpp=(gifhed.misc&7)+1;
numcols=pwr2[bpp];
return(1);
}


void readcolmap(FILE *in)
{
int f;

memset(palette,0,768);

for(f=0;f<numcols;f++)
  {
  palette[f*3  ]=(unsigned char)fgetc(in);
  palette[f*3+1]=(unsigned char)fgetc(in);
  palette[f*3+2]=(unsigned char)fgetc(in);
  }
}


int readimagehed(FILE *in)
{
int c,f;

c=fgetc(in);
while(c=='!')      /* oh damn it, they've put an ext. block in, ditch it */
  {
  fgetc(in);       /* function code, means nothing to me */
  c=fgetc(in);
  while(c!=0)
    {    /* well then, c = number of bytes, so ignore that many */
    for(f=1;f<=c;f++) fgetc(in);
    c=fgetc(in);
    }
  c=fgetc(in);     /* test for image again */
  }
if(c!=',')
  return(0);

fread(&imagehed,sizeof(imagehed),1,in);

local_colour_map=(imagehed.misc&128)?1:0;

if(!local_colour_map && !global_colour_map)
  return(0);

if((imagehed.misc&64)!=0)
  {
  interlaced=1;
  passnum=1;
  passyloc=0;
  passstep=8;
  }
else
  interlaced=0;

width=(imagehed.wide_lo+256*imagehed.wide_hi);
height=(imagehed.high_lo+256*imagehed.high_hi);

if(local_colour_map) numcols=pwr2[(imagehed.misc&7)+1];

return(1);
}


int decompress(FILE *in)
{
int csize,orgcsize;
int newcode,oldcode,k;
int first=1;

csize=fgetc(in)+1;
orgcsize=csize;
inittable(orgcsize);

oldcode=newcode=0;

while((newcode!=dc_eoi)&&(!stoprightnow))
  {
  if(!readcode(&newcode,csize,in)) return(0);
  if(newcode!=dc_eoi)
    {
    if(newcode==dc_cc)
      {
      /* don't redo it if it's the first code */
      if(!first) inittable(orgcsize);
      csize=orgcsize;
      if(!readcode(&newcode,csize,in)) return(0);
      oldcode=newcode;
      outputstring(newcode);
      }
    else
      {
      if(st_chr[newcode]!=UNUSED)
        {
        outputstring(newcode);
        k=findfirstchr(newcode);
        }
      else
        {
        k=findfirstchr(oldcode);
        outputstring(oldcode);
        outputchr(k);
        }
      if(st_last!=4095)
        {
        if(!addstring(oldcode,k))
          return(0);
        if(st_last==(pwr2[csize]-1))
          {
          csize++;
          if(csize==13) csize=12;
          }
        }
      oldcode=newcode;
      }
    }
  
  first=0;
  }

return(1);
}


void inittable(int orgcsize)
{
int f;
int numcols=(1<<(orgcsize-1));

for(f=0;f<MAXSTR;f++)
  {
  st_chr[f]=UNUSED;
  st_ptr[f]=UNUSED;
  }
for(f=0;f<numcols+2;f++)
  {
  st_ptr[f]=UNUSED;     /* these are root values... no back pointer */
  st_chr[f]=f;          /* for numcols and numcols+1, doesn't matter */
  }
st_last=numcols+1;      /* last occupied slot */
dc_cc=numcols;
dc_eoi=numcols+1;
if(numcols==2)
  {
  st_chr[2]=st_chr[3]=UNUSED;
  dc_cc=4;
  dc_eoi=5;
  st_chr[dc_cc]=dc_cc; st_chr[dc_eoi]=dc_eoi;
  st_last=5;
  }
}


/* add a string specified by oldstring + chr to string table */
int addstring(int oldcode,int chr)
{
st_last++;
if((st_last&4096))
  {
  st_last=4095;
  return(1);		/* not too clear it should die or not... */
  }
while(st_chr[st_last]!=UNUSED)
  {
  st_last++;
  if((st_last&4096))
    {
    st_last=4095;
    return(1);
    }
  }
st_chr[st_last]=chr;
if(st_last==oldcode)
  return(0);			/* corrupt GIF */
st_ptr[st_last]=oldcode;
if(st_ptr[oldcode]==UNUSED)          /* if we're pointing to a root... */
  st_ptr1st[st_last]=oldcode;        /* then that holds the first char */
else                                 /* otherwise... */
  st_ptr1st[st_last]=st_ptr1st[oldcode]; /* use their pointer to first */

return(1);
}


/* read a code of bitlength numbits from in file */
int readcode(int *newcode,int numbits,FILE *in)
{
int bitsfilled,got;

bitsfilled=got=0;
(*newcode)=0;

while(bitsfilled<numbits)
  {
  if(dc_bitsleft==0)        /* have we run out of bits? */
    {
    if(blocksize<=0)        /* end of block? */
      blocksize=fgetc(in);  /* start new block, blocksize = num of bytes */
    blocksize--;
    dc_bitbox=fgetc(in);    /* read eight more bits */
    if(feof(in))
      return(0);
    dc_bitsleft=8;
    }
  if(dc_bitsleft<(numbits-bitsfilled))
    got=dc_bitsleft;
  else
    got=numbits-bitsfilled;
  (*newcode)|=((dc_bitbox&(pwr2[got]-1))<<bitsfilled);
  dc_bitbox>>=got;
  dc_bitsleft-=got;
  bitsfilled+=got;
  }

if((*newcode)<0 || (*newcode)>MAXSTR-1) return(0);
return(1);
}


/* used to have a recursive version, but since I switched to this
 * version in zgv I thought I'd use the same one here.
 */
void outputstring(int code)
{
static int buf[MAXSTR];
int *ptr=buf;

while(st_ptr[code]!=UNUSED && ptr<buf+MAXSTR)
  {
  *ptr++=st_chr[code];
  code=st_ptr[code];
  }

outputchr(st_chr[code]);
while(ptr>buf)
  outputchr(*--ptr);
}


void outputchr(int code)
{
if(!stoprightnow)
  {
  unsigned char *ptr=image+3*((interlaced?passyloc:imagey)*width+imagex);
  
  *ptr++=palette[code*3  ];
  *ptr++=palette[code*3+1];
  *ptr  =palette[code*3+2];
  
  imagex++;
  if(imagex>=width)
    {
    imagex=0;
    imagey++;
    if(interlaced)
      {
      passyloc+=passstep;
      while(passyloc>=height && passnum<4)
        {
        passnum++;
        passyloc=(1<<(4-passnum));
        passstep=(1<<(5-passnum));
        }
      }
    if(imagey==height) stoprightnow=1;
    }
  }
}


int findfirstchr(int code)
{
if(st_ptr[code]!=UNUSED)       /* not first? then use brand new st_ptr1st! */
  code=st_ptr1st[code];                /* now with no artificial colouring */
return(st_chr[code]);
}
