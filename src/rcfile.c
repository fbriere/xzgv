/* xzgv 0.8 - picture viewer for X, with file selector.
 * Copyright (C) 1999-2003 Russell Marks. See main.c for license details.
 *
 * rcfile.c - config file handling.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include "getopt.h"	/* for getopt_long() */
#include "rcfile.h"


/* stuff for geometry. The default geometry must not specify
 * a position using `-' (and it shouldn't specify a position anyway :-)).
 */

#define DEFAULT_GEOMETRY	"92%x85%"

int mainwin_x,mainwin_y;	/* position */
int mainwin_w,mainwin_h;	/* size */
int mainwin_flags;		/* says which of we have (with GEOM_BITS_*) */

int default_sel_width=200;	/* initial selector width (also used for ~) */


/* configuration vars are declared and given default values here... */

int zoom=0;			/* fit picture to window */
int zoom_reduce_only=0;		/* when zooming, shrink only (don't enlarge) */
int interp=0;			/* interpolate pixels when scaling */
int have_statusbar=0;		/* show statusbar */
int tn_msgs=0;			/* say when reading thumbnails (on sbar) */
int thin_rows=0;		/* use lower row height */
int auto_hide=0;		/* auto-hide selector */
int revert=1;			/* revert to normal scale/interp on pic load */
int revert_orient=1;		/* revert to normal orientation on pic load */
int fullscreen=0;		/* take up whole screen for window */
int show_tagged=0;		/* echo currently-tagged files on exit */
int fast_recursive_update=0;	/* if 0, use blocking tn update (slow :-/) */
int hicol_dither=-1;		/* 15/16-bit dither. 0=don't, 1=do, -1=n/a;
				   default is whatever backend decides on */
int invert_logo=0;		/* invert logo to look better w/dark themes */
int skip_parent=0;		/* skip cursor past .. on initial dir */
int click_nextpic=1;		/* click on viewer = next pic */
int mouse_scale_x=0;		/* ctrl-click scales x if true, else y */
double picgamma=1.0;		/* `gamma' name already used :-/ */
enum sorttypes filesel_sorttype=sort_name;	/* sort order */
int image_bigness_threshold=2000000; /* images >= this num pixels are `big' */
int delete_single_prompt=1;	/* prompt for deleting a single file */
int careful_jpeg=0;		/* enable (don't disable) fancy upsampling */
int sort_timestamp_type=0;	/* 0=mtime, 1=ctime, 2=atime */
int use_exif_orient=0;		/* use Exif orientation tag for JPEGs */


/* now non-config vars :-) */

static int line;		/* current line in config file */
int in_config=0;		/* true if reading config file */
char *config_file_name=NULL;	/* full path to config file used */


/* required prototypes */
void get_bool(char *arg,void *dataptr);
void get_geom(char *arg,void *data);
void get_selwidth(char *arg,void *data);
void get_sortorder(char *arg,void *data);
void get_timetype(char *arg,void *data);
void get_int(char *arg,void *data);
void get_double(char *arg,void *data);
void print_version(char *arg,void *dataptr);
void print_gtk_ver(char *arg,void *dataptr);
void usage_help(char *arg,void *dataptr);

 

struct cfglookup_tag
  {
  char *name;
  int allow_config;		/* if zero, only here for cmdline opt lookup */
  void (*funcptr)(char *,void *);
  void *dataptr;
  };


/* include opt/var tables generated from options.src by mkopts.awk;
 * rcfile_var.h has to be included after defs of above struct and
 * some prototypes, so may as well keep them together.
 */
#include "rcfile_opt.h"
#include "rcfile_var.h"

/* and this one defines the short-option string. */
#include "rcfile_short.h"


/* macro for `xzgv:'-ish error prefix, the format of which should
 * differ when reading a config file.
 */
#define CONFIG_ERR_PREFIX() \
	do \
	  { \
	  if(in_config) \
	    fprintf(stderr,"xzgv:%s:%d: ",config_file_name,line); \
	  else \
	    fprintf(stderr,"xzgv: "); \
	  } \
	while(0)



/* find next char which isn't NUL, space, or tab. */
void find_token(char **ptr)
{
while(*(*ptr) && strchr(" \t",*(*ptr))!=NULL)
  (*ptr)++;
}


int token_length(char *ptr)
{
char *start=ptr;

while(*ptr && strchr(" \t",*ptr)==NULL)
  ptr++;

return(ptr-start);
}


/* returns 1 if equal, 0 otherwise */
int token_compare(char *tptr,char *txt)
{
int tlen;

tlen=token_length(tptr);
if(tlen!=strlen(txt))
  return(0);

return(strncmp(tptr,txt,tlen)==0);
}


void parse_config(FILE *in)
{
static char inpline[1024];
char *ptr;
int f,c,inpc,found=0;

line=0;

while(fgets(inpline,sizeof(inpline),in)!=NULL) 
  {
  line++;
  
  if(inpline[strlen(inpline)-1]=='\n') inpline[strlen(inpline)-1]=0;
  if((ptr=strchr(inpline,'#'))!=NULL)
    *ptr=0;
  
  if(*inpline)
    {
    found=0;
    ptr=inpline;
    find_token(&ptr);
    inpc=*ptr;
    
    for(f=0;(c=cfglookup[f].name[0]);f++)
      if(inpc==c && token_compare(ptr,cfglookup[f].name) &&
         cfglookup[f].allow_config)
        {
        ptr+=token_length(ptr);		/* skip current token */
	find_token(&ptr);		/* find next (if any) */
        if(*ptr==0)
          ptr=NULL;
        
        /* we supply the token (or lack of one) whether they asked
         * for it or not - it's up to them whether to use it or ignore it.
         * first arg is NULL if no arg was present.
         */
        (*cfglookup[f].funcptr)(ptr,cfglookup[f].dataptr);
        found=1;
        break;
        }
    
    if(!found)
      {
      CONFIG_ERR_PREFIX();
      fprintf(stderr,"bad variable name.\n");
      exit(1);
      }
    }
  }
}


void get_config()
{
static char cfgfile[1024];
FILE *in;
int got_rcfile=0;
char *home=getenv("HOME");

/* get initial geometry in place */
get_geom(DEFAULT_GEOMETRY,NULL);

in_config=1;

*cfgfile=0;
if(home && strlen(home)<sizeof(cfgfile)-strlen("/.xzgvrc")-1)
  sprintf(cfgfile,"%s/.xzgvrc",home);

if((in=fopen(cfgfile,"r"))!=NULL)
  got_rcfile=1,config_file_name="~/.xzgvrc";	/* shortened name for that */
else if((in=fopen(config_file_name="/etc/xzgv.conf","r"))!=NULL)
  got_rcfile=1;

if(got_rcfile)
  {
  parse_config(in);
  fclose(in);
  }

in_config=0;
}


void get_bool(char *arg,void *data)
{
int *boolptr=(int *)data;

if(!arg)
  {
  if(!in_config)
    {
    /* if no arg, and not in config file, set the flag. */
    *boolptr=1;
    return;
    }
  else	/* in config file, so it's an error */
    {
    CONFIG_ERR_PREFIX();
    fprintf(stderr,"option-setting arg (on/off, yes/no, ...) missing.\n");
    exit(1);
    }
  }

/* otherwise, set depending on value given. */
if(token_compare(arg,"on") || *arg=='y' || *arg=='1')
  *boolptr=1;
else
  if(token_compare(arg,"off") || *arg=='n' || *arg=='0')
    *boolptr=0;
  else
    {
    CONFIG_ERR_PREFIX();
    fprintf(stderr,
            "bad option-setting arg\n\t(use on/off, y/n, yes/no, or 1/0).\n");
    exit(1);
    }
}


/* caller must supply valid values at str, wp, and hp
 * (latter two (default width/height) are needed in case they use a `-',
 * specifying gap between right/bottom of window and right/bottom edge
 * of screen)
 */
void geom_parse(char *str,int *wp,int *hp,int *xp,int *yp,int *flagsp)
{
char *ptr=str,*oldptr;
int flags=0;
int next_is_y=0,next_is_h=0;
int next_is_pos=0;
double num;
int scrnw=(float)gdk_screen_width();
int scrnh=(float)gdk_screen_height();
int i,sub;

if(ptr==NULL) return;

while(*ptr)
  {
  if(!strchr("0123456789+-x.%",*ptr))
    {
    ptr++;
    continue;
    }
  
  sub=0;
  if(*ptr=='x')
    {
    next_is_h=1,ptr++;
    continue;
    }
  
  if(*ptr=='+' || *ptr=='-')
    {
    if(*ptr=='-') sub=1;
    ptr++;
    
    if(next_is_pos)
      next_is_y=1;
    else
      next_is_pos=1;
    }
  
  if(isdigit(*ptr) || *ptr=='+' || *ptr=='-' || *ptr=='%')
    {
    oldptr=ptr;
    
    num=strtod(ptr,&ptr);
    
    /* give up if we'd otherwise get stuck on a non-number */
    if(ptr==oldptr)
      break;
    
    if(*ptr=='%')
      {
      ptr++;
      num=num/100.*((next_is_y || next_is_h)?scrnh:scrnw);
      }
    
    i=(int)(num+0.5);
    
    if(sub)		/* subtract from screen width/height */
      {
      if(next_is_y)
        i=scrnh-i-*hp;
      else
        i=scrnw-i-*wp;
      }
    
    if(next_is_pos)
      {
      if(next_is_y)
        *yp=i,flags|=GEOM_BITS_Y_SET;
      else
        *xp=i,flags|=GEOM_BITS_X_SET;
      }
    else
      if(next_is_h)
        *hp=i,flags|=GEOM_BITS_H_SET;
      else
        *wp=i,flags|=GEOM_BITS_W_SET;
    }
  }

*flagsp=flags;
}


/* get geometry */
void get_geom(char *arg,void *data)
{
if(arg==NULL)
  {
  CONFIG_ERR_PREFIX();
  fprintf(stderr,"missing geometry-specifying arg.\n");
  exit(1);
  }

geom_parse(arg,&mainwin_w,&mainwin_h,&mainwin_x,&mainwin_y,&mainwin_flags);
}


/* get selector width */
/* XXX doesn't do window percentage yet */
void get_selwidth(char *arg,void *data)
{
int tmp=arg?atoi(arg):0;
int *ptr=(int *)data;

if(arg==NULL)
  {
  CONFIG_ERR_PREFIX();
  fprintf(stderr,"missing selector width arg.\n");
  exit(1);
  }

if(tmp<0) tmp=0;
if(tmp>32767) tmp=32767;

*ptr=tmp;
}


/* get sort order, determined by 1st letter of arg */
void get_sortorder(char *arg,void *data)
{
if(arg==NULL)
  {
  CONFIG_ERR_PREFIX();
  fprintf(stderr,"missing sort-order arg.\n");
  exit(1);
  }

switch(tolower(*arg))
  {
  case 'n':
    filesel_sorttype=sort_name;
    break;
  case 'e':
    filesel_sorttype=sort_ext;
    break;
  case 's':
    filesel_sorttype=sort_size;
    break;
  case 'd': case 't':
    filesel_sorttype=sort_time;
    break;

  default:
    CONFIG_ERR_PREFIX();
    fprintf(stderr,"unrecognised sort order.\n");
    exit(1);
  }
}


/* get timestamp type for sort-by-time&date sort order,
 * as determined by 1st letter of arg.
 */
void get_timetype(char *arg,void *data)
{
if(arg==NULL)
  {
  CONFIG_ERR_PREFIX();
  fprintf(stderr,"missing timestamp type arg.\n");
  exit(1);
  }

switch(tolower(*arg))
  {
  case 'm':
    sort_timestamp_type=0;
    break;
  case 'c':
    sort_timestamp_type=1;
    break;
  case 'a':
    sort_timestamp_type=2;
    break;

  default:
    CONFIG_ERR_PREFIX();
    fprintf(stderr,"unrecognised timestamp type.\n");
    exit(1);
  }
}


void get_int(char *arg,void *data)
{
int *ptr=(int *)data;

if(arg==NULL)
  {
  CONFIG_ERR_PREFIX();
  fprintf(stderr,"missing arg.\n");
  exit(1);
  }

*ptr=atoi(arg);
}


void get_double(char *arg,void *data)
{
double *ptr=(double *)data;

if(arg==NULL)
  {
  CONFIG_ERR_PREFIX();
  fprintf(stderr,"missing arg.\n");
  exit(1);
  }

*ptr=atof(arg);
}


int parse_options(int argc,char *argv[])
{
const char *name;	/* const needed because of struct option declaration */
int entry,ret,f,c,namec,found;

do
  {
  /* the GNU libc docs don't make it clear whether optarg is set to NULL
   * when a *short* option doesn't have an arg, so I play it safe here.
   */
  optarg=NULL;
  
  /* SHORTOPT_STRING is defined in rcfile_short.h, as gen'd from options.src */
  ret=getopt_long(argc,argv,SHORTOPT_STRING,long_opts,&entry);
  
  if(ret=='?')
    {
    /* no need for error message, it's already been done */
    exit(1);
    }
  
  if(ret!=-1)
    {
    /* if we have a short option, it returns char code; find relevant
     * long-option entry. It also returns char code for long options
     * with equivalent short option - it doesn't hurt to look
     * it up in that case as well though. It's not like this takes huge
     * amounts of CPU... ;-)
     */
    if(isalnum(ret))
      {
      found=0;
      
      for(f=0;long_opts[f].name;f++)
        {
        if(long_opts[f].val==ret)
          {
          entry=f;
          found=1;
          break;
          }
        }
      
      if(!found)
        {
        fprintf(stderr,
                "short option not found in long_opts[] - can't happen!\n");
        continue;
        }
      }
    
    /* now we have a valid entry in long_opts[], lookup name in
     * cfglookup to get funcptr/dataptr and run the function.
     */
    name=long_opts[entry].name;
    namec=*name;
    found=0;
    
    for(f=0;(c=cfglookup[f].name[0]);f++)
      if(namec==c && strcmp(name,cfglookup[f].name)==0)
        {
        (*cfglookup[f].funcptr)(optarg,cfglookup[f].dataptr);
        found=1;
        break;
        }
    
    if(!found)
      fprintf(stderr,
              "long option not found in cfglookup[] - can't happen!\n");
    }
  }
while(ret!=-1);

return(argc-optind);
}


void print_version(char *arg,void *dataptr)
{
printf("xzgv " XZGV_VER "\n");
exit(0);
}


void print_gtk_ver(char *arg,void *dataptr)
{
printf("GTK+ %d.%d.%d\n",
       gtk_major_version,gtk_minor_version,gtk_micro_version);
exit(0);
}


void usage_help(char *arg,void *dataptr)
{
printf("xzgv " XZGV_VER
       " - copyright (c) 1999-2003 Russell Marks.\n");
puts(
"usage: xzgv [options] [dir | file ...]\n"
"\n"
"   -a	--auto-hide	automatically hide selector on selecting a picture.\n"
"	--careful-jpeg	enable JPEG `fancy upsampling' (see info file\n"
"			or man page).\n"
"	--delete-single-prompt\n"
"			(normally enabled, use --delete-single-prompt=off to\n"
"			disable) if *disabled*, don't prompt for confirmation\n"
"			when deleting a file.\n"
"	--dither-hicol	use dithering in 15/16-bit to increase apparent\n"
"			colour depth, whatever Imlib's default setting is.\n"
"			You can also use `--dither-hicol=off' to disable\n"
"			this if you normally have Imlib use it.\n"
"	--exif-orient	in JPEG files, use Exif orientation tags (inserted\n"
"			by e.g. digital cameras) to correct image orientation\n"
"			before display.\n"
"	--fast-recursive-update\n"
"			when doing recursive thumbnail update, don't\n"
"			read visible thumbnails for a directory before\n"
"			doing the update (only slightly faster).\n"
"   -f	--fullscreen	use the whole screen for the xzgv window, without\n"
"			even window-manager decorations if possible. (But\n"
"			your wm may not care to trust borderless programs.)\n"
"   -g	--geometry geom\n"
"			use geometry `geom'. For example, `400x300' specifies\n"
"			window size in pixels, `70%x50%' specifies size as\n"
"			percentage of screen width/height, `+100+50' specifies\n"
"			position relative to top-left, and `50%x30%-30%-20%'\n"
"			is left as an exercise for the reader. :-) The default\n"
"			geometry is `92%x85%'.\n"
"			(See info file or man page for more details.)\n"
"   -G	--gamma val	set gamma adjustment to `val'. The default is 1.0, i.e.\n"
"			no adjustment. (See info file or man page for details,\n"
"			and a discussion of gamma issues.)\n"
"   -h	--help		give this usage help.\n"
"	--image-bigness-threshold numpix\n"
"			set the boundary `numpix' above which images are\n"
"			considered `big', and rendered piece-by-piece rather\n"
"			than all-at-once (which is nicer, but harder on\n"
"			memory). Units are number of pixels in image (i.e.\n"
"			width times height), and the default is 2000000 pixels.\n"
"	--interpolate	interpolate between the picture's pixels when\n"
"			scaling up. Usually looks nicer, but it's slow.\n"
"	--mouse-scale-x	if enabled, control-click scales only the X axis -\n"
"			the default is to scale only the Y axis.\n"
"	--revert-orient	(normally enabled, use --revert-orient=off to disable)\n"
"			if *disabled*, orientation (flip/mirror/rotate) state\n"
"			is retained between pictures.\n"
"	--revert-scale	(normally enabled, use --revert-scale=off to disable)\n"
"			if *disabled*, scaling is retained between pictures.\n"
"	--selector-width width\n"
"			set initial/default selector width to `width'. (The\n"
"			units used are pixels, and the normal setting 200.)\n"
"   -T	--show-tagged	show names of tagged files on exit (they're listed\n"
"			to stdout).\n"
"	--show-thumbnail-messages\n"
"			show on the status bar when thumbnails are being read.\n"
"			The status bar must be enabled for the messages to be\n"
"			visible, of course. :-)\n"
"   -k	--skip-parent	for the first directory shown, skip the cursor past\n"
"			`..' (the parent dir). Can be useful when you'd like\n"
"			to immediately use space to `page' through the dir.\n"
"   -o	--sort-order	set initial sorting order used in the selector.\n"
"			Types are `name', `ext', `size', `date' (or `time');\n"
"			only the first char (n/e/s/d/t) need be given.\n"
"			(The default is name order.)\n"
"	--sort-timestamp-type type\n"
"			set timestamp type to use when using time/date sorting\n"
"			order. Types are `mtime' (default), `ctime', and\n"
"			`atime'; only the first char (m/c/a) need be given.\n"
"	--statusbar	show a status bar below the selector; this, for\n"
"			example, says when a picture is being read.\n"
"   -t	--thin-rows	use rows a third the normal height in the selector.\n"
"			This can be very useful on lower-resolution screens,\n"
"			or if you're really interested in filenames, not\n"
"			thumbnails.\n"
"   -v	--version	report version number.\n"
"	--version-gtk	report version of GTK+ being used by xzgv.\n"
"   -z	--zoom		fit pictures in the viewer window, whatever their\n"
"			actual size.\n"
"   -r	--zoom-reduce-only\n"
"			when zooming, only *reduce* pictures to fit; i.e.\n"
"			make big pictures viewable all-at-once while leaving\n"
"			small pictures intact.\n"
"\n"
"	dir		start xzgv on a certain directory.\n"
"	file ...	view (only) the file(s) specified.\n"
"\n"
"All options are processed after any ~/.xzgvrc or /etc/xzgv.conf file.\n"
"Most long options (minus `--') can used in either file with e.g. `zoom on'.\n"
"\n"
"On/off settings (such as zoom) are enabled by e.g. `-z' or `--zoom';\n"
"however, the long-option form `--option=off' can be used to disable\n"
"them (needed when they are enabled by default - revert-scale, for\n"
"example - or to override them being enabled in a config file).\n"
"\n"
"(This syntax actually lets you both disable *and* enable options,\n"
"using (for the arg after `=') on/off, y/n, yes/no, or 1/0.)");

exit(0);
}
