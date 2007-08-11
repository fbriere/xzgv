/* xzgv 0.8 - picture viewer for X, with file selector.
 * Copyright (C) 1999-2003 Russell Marks. See main.c for license details.
 *
 * readjpeg.c - read JPEG files. Based on zgv's readjpeg.c.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/file.h>  /* for open et al */
#include <jpeglib.h>

#include "rcfile.h"
#include "reader.h"

#include "readjpeg.h"


/* the default setting is good (and fast) enough that it's difficult to
 * see any point in complicating matters by having a config file option
 * for it.
 */
#define JPEG_IDX_SIZE	4	/* like zgv's `jpegindexstyle 2' */



/* The way that libjpeg v5 works means that this more-or-less *has* to
 * global if I want to allow aborting in mid-read. Oh well. :-(
 */
static struct jpeg_decompress_struct cinfo;

/* stuff for error routines */
struct my_error_mgr
  {
  struct jpeg_error_mgr pub;	/* "public" fields */
  jmp_buf setjmp_buffer;	/* for return to caller */
  };

typedef struct my_error_mgr * my_error_ptr;


void my_error_exit(j_common_ptr cinfo)
{
my_error_ptr myerr=(my_error_ptr) cinfo->err;

/* cleanup is done after jump back, so just do that now... */
longjmp(myerr->setjmp_buffer, 1);
}


/* No warning messages */
void my_output_message(j_common_ptr cinfo)
{
}



static unsigned long read_tiff_long(FILE *in,int little_endian)
{
unsigned long ret;

if(little_endian)
  {
  ret=fgetc(in);
  ret|=(fgetc(in)<<8);
  ret|=(fgetc(in)<<16);
  ret|=(fgetc(in)<<24);
  }
else
  {
  ret=(fgetc(in)<<24);
  ret|=(fgetc(in)<<16);
  ret|=(fgetc(in)<<8);
  ret|=fgetc(in);
  }

return(ret);
}


static unsigned int read_tiff_short(FILE *in,int little_endian)
{
unsigned int ret;

if(little_endian)
  {
  ret=fgetc(in);
  ret|=(fgetc(in)<<8);
  }
else
  {
  ret=(fgetc(in)<<8);
  ret|=fgetc(in);
  }

return(ret);
}


int get_exif_orientation(char *filename)
{
static int xzgv_orient_lookup[]={0,1,3,2,7,4,6,5};
FILE *in;
unsigned char buf[4];
long ifd0_ofs;
int little_endian,f,num_ents;
int exif_orient;

if((in=fopen(filename,"rb"))==NULL)
  return(0);

fread(buf,1,4,in);
if(memcmp(buf,"\xff\xd8\xff\xe1",4)!=0)
  {
  fclose(in);
  return(0);
  }

/* we can ignore data size */
fgetc(in);
fgetc(in);

fread(buf,1,4,in);
if(memcmp(buf,"Exif",4)!=0 || fgetc(in) || fgetc(in))
  {
  fclose(in);
  return(0);
  }

/* now at tiff header */
fread(buf,1,4,in);
if(memcmp(buf,"II*\0",4)!=0 && memcmp(buf,"MM\0*",4)!=0)
  {
  fclose(in);
  return(0);
  }

little_endian=(memcmp(buf,"II*\0",4)==0);

/* orientation is an IFD0 tag, so read IFD0 offset and seek */
ifd0_ofs=12+read_tiff_long(in,little_endian);
fseek(in,ifd0_ofs,SEEK_SET);

/* search IFD0 for Orientation (0x112) */
exif_orient=-1;
num_ents=read_tiff_short(in,little_endian);
for(f=0;f<num_ents;f++)
  {
  fseek(in,ifd0_ofs+2+12*f,SEEK_SET);
  if(read_tiff_short(in,little_endian)==0x112 &&
     read_tiff_short(in,little_endian)==3 &&
     read_tiff_long(in,little_endian)==1)
    {
    exif_orient=read_tiff_short(in,little_endian);
    break;
    }
  }

fclose(in);

if(exif_orient<1 || exif_orient>8)
  return(0);

/* return, converting Exif orientation to xzgv format */

return(xzgv_orient_lookup[exif_orient-1]);
}


/* origwp/orighp can be NULL if not interested */
int read_jpeg_file(char *filename,unsigned char **imagep,
                   int *wp,int *hp,int *origwp,int *orighp,int for_tn)
{
static FILE *in;
struct my_error_mgr jerr;
static unsigned char **lineptrs;
static int have_image;
static int width,height;
static unsigned char *image;
static int cmyk;
unsigned char *ptr,*ptr2;
int chkw,chkh;
int f,rec;
static int greyscale;	/* static to satisfy gcc -Wall */

cmyk=0;
greyscale=0;

lineptrs=NULL;
have_image=0;

if((in=fopen(filename,"rb"))==NULL)
  return(0);

cinfo.err=jpeg_std_error(&jerr.pub);
jerr.pub.error_exit=my_error_exit;
jerr.pub.output_message=my_output_message;

if(setjmp(jerr.setjmp_buffer))
  {
  if(lineptrs) free(lineptrs);
  jpeg_destroy_decompress(&cinfo);
  fclose(in);
  /* if we have an image, ignore the error */
  if(have_image) goto finish;
  return(0);
  }

/* Now we can initialize the JPEG decompression object. */
jpeg_create_decompress(&cinfo);

jpeg_stdio_src(&cinfo,in);	/* indicate source is the file */

jpeg_read_header(&cinfo,TRUE);

/* setup parameters for decompression */

/* fix to greys if greyscale - this is required to read greyscale JPEGs */
if(cinfo.jpeg_color_space==JCS_GRAYSCALE)
  {
  cinfo.out_color_space=JCS_GRAYSCALE;
  cinfo.desired_number_of_colors=256;
  cinfo.quantize_colors=FALSE;
  cinfo.two_pass_quantize=FALSE;
  greyscale=1;
  }

if(cinfo.jpeg_color_space==JCS_CMYK)
  cmyk=1;

if(cinfo.jpeg_color_space==JCS_YCCK)
  {
  cmyk=1;
  cinfo.out_color_space=JCS_CMYK;
  }

*wp=width=cinfo.image_width;
*hp=height=cinfo.image_height;

if(origwp) *origwp=width;
if(orighp) *orighp=height;

if(for_tn)
  {
  chkw=JPEG_IDX_SIZE*80;
  chkh=JPEG_IDX_SIZE*60;

  cinfo.scale_num=1;
  cinfo.scale_denom=1;

  while((width>chkw || height>chkh) && cinfo.scale_denom<8)
    {
    width/=2;
    height/=2;
    cinfo.scale_denom*=2;
    }

  cinfo.dct_method=JDCT_FASTEST;
  cinfo.do_fancy_upsampling=FALSE;

  jpeg_calc_output_dimensions(&cinfo);

  *wp=width=cinfo.output_width;
  *hp=height=cinfo.output_height;
  }

/* this turns out to be the key reason Imlib1 was loading JPEGs
 * faster. This is what is technically known as "cheating", you gits. ;-)
 * Now I suppose I have to default to it for consistency, which is awkward
 * as it wouldn't have been my first choice for a default...
 */
if(!careful_jpeg)
  cinfo.do_fancy_upsampling=FALSE;

/* this one shouldn't hurt */
cinfo.do_block_smoothing=FALSE;

if(WH_BAD(width,height) || (*imagep=image=malloc(width*(height+cmyk)*3))==NULL)
  longjmp(jerr.setjmp_buffer,1);

jpeg_start_decompress(&cinfo);

/* read the image */
if((lineptrs=malloc(height*sizeof(unsigned char *)))==NULL)
  longjmp(jerr.setjmp_buffer,1);

ptr=image+width*2*greyscale;	/* put data at end of line if greyscale */
for(f=0;f<height;f++,ptr+=width*3)
  lineptrs[f]=ptr;

if(!cmyk)
  {
  rec=cinfo.rec_outbuf_height;
  while(cinfo.output_scanline<height)
    {
    f=height-cinfo.output_scanline;
    jpeg_read_scanlines(&cinfo,lineptrs+cinfo.output_scanline,
                        f>rec?rec:f);
    }
  }
else	/* cmyk output */
  {
  int tmp;

  ptr=image;
  while(cinfo.output_scanline<height)
    {
    jpeg_read_scanlines(&cinfo,&ptr,1);
    ptr2=ptr;
    for(f=0;f<width;f++,ptr+=3,ptr2+=4)
      {
      tmp=ptr2[3];
      ptr[0]=(tmp*ptr2[0])/255;
      ptr[1]=(tmp*ptr2[1])/255;
      ptr[2]=(tmp*ptr2[2])/255;
      }
    }
  }

free(lineptrs);
lineptrs=NULL;

/* this lets us tolerate post-image corruption to a certain extent */
have_image=1;

jpeg_finish_decompress(&cinfo);
jpeg_destroy_decompress(&cinfo);
fclose(in);

finish:
if(greyscale)
  {
  int x,y;
  
  ptr=image+width*2*greyscale;
  ptr2=image;
  
  /* convert greyscale to RGB */
  for(y=0;y<height;y++,ptr+=width*2)
    for(x=0;x<width;x++)
      {
      *ptr2++=*ptr;
      *ptr2++=*ptr;
      *ptr2++=*ptr++;
      }
  }

return(1);
}
