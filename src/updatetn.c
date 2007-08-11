/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * updatetn.c - code for file details dialog.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "backend.h"
#include "main.h"
#include "rcfile.h"		/* for thin_rows */
#include "dither.h"
#include "resizepic.h"
#include "confirm.h"
#include "misc.h"

#include "updatetn.h"


/* stuff for checking old directories (to avoid symlink loops) */
struct olddir_tag
  {
  dev_t device;
  ino_t inode;
  };

static struct olddir_tag *olddirs=NULL;
static int olddir_byte_size=64*sizeof(struct olddir_tag);
static int olddir_byte_incr=32*sizeof(struct olddir_tag);
static int num_olddirs=0;



/* allocate some initial space for the olddirs[] array. */
void olddir_init()
{
if(olddirs!=NULL) return;	/* sanity check */

if((olddirs=malloc(olddir_byte_size))==NULL)
  quit_no_mem();

num_olddirs=0;
}


/* make olddirs bigger if needed.
 * call this *before writing each new entry*.
 */
void olddir_resize_if_needed(int newent)
{
/* this is absurdly conservative, just to be on the safe side :-) */
if((newent+1)*sizeof(struct olddir_tag)>=olddir_byte_size)
  {
  olddir_byte_size+=olddir_byte_incr;
  if((olddirs=realloc(olddirs,olddir_byte_size))==NULL)
    quit_no_mem();
  }
}


void olddir_uninit(void)
{
free(olddirs);
olddirs=NULL;
num_olddirs=0;
}


int makexv332(char *filename,char *xvpicfn,
              unsigned char **xvpic_data,int *wp,int *hp,int *written_ok_ptr)
{
FILE *out;
int w,h,y;
unsigned char *smallpic;
xzgv_image *origpic;
int width,height;
int origw,origh;
int allow_crunch=1;
int written_ok=0;

/* read pic */
if((origpic=load_image(filename,1,&origw,&origh))==NULL)
  return(0);

/* only allow crude resizing before nice resizing if that hasn't
 * already (in effect) been done.
 */
if(origpic->w!=origw || origpic->h!=origh)
  allow_crunch=0;

/* resize */
w=80; h=60;
smallpic=resizepic(origpic->rgb,
                   width=origpic->w,height=origpic->h,&w,&h,
                   allow_crunch);

backend_image_destroy(origpic);		/* finished with this */

/* dither */
ditherinit(w);
for(y=0;y<h;y++)
  ditherline(smallpic+y*80*3,y,w);
ditherfinish();

/* write */
out=fopen(xvpicfn,"wb");	/* keep going even if we can't write */

/* try to be Gimp-friendly, but we can't tell what type the pic really is,
 * so just presume it's RGB. :-/
 */
if(out)
  {
  written_ok=1;		/* bit bogus, should check writes too */
  
  fprintf(out,"P7 332\n");
  fprintf(out,"#IMGINFO:%dx%d RGB\n",origw,origh);
  fprintf(out,"#END_OF_COMMENTS\n");
  fprintf(out,"%d %d 255\n",w,h);
  
  for(y=0;y<h;y++)
    fwrite(smallpic+y*80*3,1,w,out);
  fclose(out);
  }

/* pack it into a w*h pixmap to return.
 * Loop starts at 1, as the 0th line is already ok.
 * (We could also realloc() to make it smaller, but there isn't
 * really any point, it's only going to be around for a short time.)
 */
for(y=1;y<h;y++)
  memcpy(smallpic+y*w,smallpic+y*80*3,w);

*xvpic_data=smallpic;
*wp=w;
*hp=h;

if(written_ok_ptr)
  *written_ok_ptr=written_ok;

return(1);
}


/* if needed, update thumbnail for file at row given. */
int update_one_tn(int row,GtkWidget **update_tn_win_ptr)
{
static char buf[1024];
FILE *test=NULL;
struct clist_data_tag *datptr;
unsigned char *xvpic_data;
GdkPixmap *pixmap,*small_pixmap;
struct stat realpic,xvpic;
int w,h;
char *ptr;
int written_ok=1;	/* only 0 if we *tried* to write one and it failed */

gtk_clist_get_text(GTK_CLIST(clist),row,SELECTOR_NAME_COL,&ptr);
datptr=gtk_clist_get_row_data(GTK_CLIST(clist),row);

/* skip dirs, files we can't stat, and hidden files */
if(datptr->isdir || stat(ptr,&realpic)==-1 || *ptr=='.')
  return(1);

strcpy(buf,".xvpics/");
strncat(buf,ptr,sizeof(buf)-8-2);	/* above string is 8 chars long */

/* if not there, or pic is newer, or thumbnail is unreadable,
 * make a thumbnail.
 */
if(stat(buf,&xvpic)==-1 || realpic.st_mtime>xvpic.st_mtime ||
   (test=fopen(buf,"rb"))==NULL)
  {
  /* make the row visible to show what we're doing */
  make_visible_if_not(row);

  /* make sure a page of thumbnails is there */
  blocking_thumbnail_read_visible(update_tn_win_ptr);
  
  /* give it a chance to redraw before loading or we end up with nasty
   * `shearing' in the meantime.
   */
  if(*update_tn_win_ptr && mainwin)
    do_gtk_stuff();
  
  /* have to try and make dir too, it may not be there;
   * result is ignored, as we're going to be making thumbnail for our
   * current use whether we can write it or not.
   */
  mkdir(".xvpics",0777);
  
  if(makexv332(ptr,buf,&xvpic_data,&w,&h,&written_ok))
    {
    /* now update pixmap, whether it wrote the thumbnail or not.
     * (makexv332()'s ret value indicates if source image existed etc.)
     */
    pixmap=xvpic2pixmap(xvpic_data,w,h,&small_pixmap);
    if(pixmap)
      {
      if(datptr->pm_norm)  gdk_pixmap_unref(datptr->pm_norm);
      if(datptr->pm_small) gdk_pixmap_unref(datptr->pm_small);
      if(datptr->pm_norm_mask)  gdk_pixmap_unref(datptr->pm_norm_mask);
      if(datptr->pm_small_mask) gdk_pixmap_unref(datptr->pm_small_mask);
      datptr->pm_norm=pixmap;
      datptr->pm_small=small_pixmap;
      datptr->pm_norm_mask=datptr->pm_small_mask=NULL;
      gtk_clist_set_pixmap(GTK_CLIST(clist),row,SELECTOR_TN_COL,
                           thin_rows?datptr->pm_small:datptr->pm_norm,
                           NULL);
      }
    
    free(xvpic_data);
    }
  }

/* close the file we (may have) opened to test readability */
if(test) fclose(test);

return(written_ok);
}


/* update current dir's thumbnails;
 * this bit is common to both normal update and recursive update.
 */
void update_common(GtkWidget **update_tn_win_ptr,GtkWidget *progbar,
                   int recursive_mode)
{
int f;

if(*update_tn_win_ptr && mainwin)
  gtk_progress_configure(GTK_PROGRESS(progbar),0.,0.,(float)numrows);

/* do GTK+ update early, in case we're running on a slow machine
 * (where the first `file' (almost certainly a dir) will take a
 * noticeable time to `update').
 */
if(*update_tn_win_ptr && mainwin)
  do_gtk_stuff();

for(f=0;f<numrows;f++)
  {
  if(!update_one_tn(f,update_tn_win_ptr) && recursive_mode)
    return;	/* unwritable thumbnails are pointless in recursive mode */
  
  /* update progress bar, and give it a chance to draw */
  if(*update_tn_win_ptr && mainwin)
    {
    gtk_progress_set_value(GTK_PROGRESS(progbar),(float)(f+1));
    do_gtk_stuff();
    }
  
  if(!*update_tn_win_ptr || !mainwin)
    break;		/* they must have aborted */
  }
}



/* returns window widget, and progbar in arg */
void make_update_win(GtkWidget **update_tn_win_ret,GtkWidget **progbar_ret)
{
GtkWidget *update_tn_win;
GtkWidget *progbar,*button;

update_tn_win=*update_tn_win_ret=gtk_dialog_new();

/* set returned pointer to NULL when destroyed */
gtk_signal_connect(GTK_OBJECT(update_tn_win),"destroy",
                   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
                   update_tn_win_ret);

gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(update_tn_win)->vbox),2);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(update_tn_win)->action_area),0);

gtk_window_set_title(GTK_WINDOW(update_tn_win),"Updating Thumbnails");
gtk_window_set_policy(GTK_WINDOW(update_tn_win),FALSE,TRUE,FALSE);
gtk_widget_set_usize(update_tn_win,250,55);
gtk_window_set_position(GTK_WINDOW(update_tn_win),GTK_WIN_POS_CENTER);
gtk_window_set_modal(GTK_WINDOW(update_tn_win),TRUE);

progbar=*progbar_ret=gtk_progress_bar_new();
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(update_tn_win)->vbox),
                   progbar,TRUE,TRUE,2);
gtk_widget_show(progbar);

button=gtk_button_new_with_label("Cancel");
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(update_tn_win)->action_area),
                   button,TRUE,TRUE,2);
gtk_signal_connect_object(GTK_OBJECT(button),"clicked",
                          GTK_SIGNAL_FUNC(gtk_widget_destroy),
                          GTK_OBJECT(update_tn_win));
gtk_widget_grab_focus(button);
gtk_widget_show(button);

/* esc also aborts (even from main window!) */
gtk_widget_add_accelerator(button,"clicked",gtk_accel_group_get_default(),
                           GDK_Escape,0,0);


gtk_widget_show(update_tn_win);
}


/* callback for updating thumbnails. This routine is basically
 * just GUI code and a loop - the real work is done by update_one_tn().
 */
void cb_update_tn(void)
{
GtkWidget *update_tn_win,*progbar;
int was_reading=0;
GtkAdjustment *clist_vadj;
float prev_vadj_value;

/* if by some miracle there are no files, don't bother ;-) */
if(!numrows) return;

/* save vertical position in clist */
clist_vadj=gtk_clist_get_vadjustment(GTK_CLIST(clist));
prev_vadj_value=clist_vadj->value;

/* remove any running thumbnail read. We'll restart it after we're done.
 * (This makes sure we pick up any not updated by this routine.)
 */
if(thumbnail_read_running())
  {
  stop_thumbnail_read();
  was_reading=1;
  }

/* make sure a page of thumbnails are visible whether we had time to
 * read them before or not.
 */
blocking_thumbnail_read_visible(NULL);

/* we include dirs in our number-of-files-done calculations. This is
 * a bit crap I s'pose, but it simplifies things, and makes sure you
 * always get *some* activity, even if it's not doing anything as such.
 */
make_update_win(&update_tn_win,&progbar);

/* have to pass update_tn_win by ref so it gets NULLed correctly */
update_common(&update_tn_win,progbar,0);

/* if they decided to quit completely, take the hint :-) */
if(!mainwin)
  return;

/* if not already destroyed, blast window */
if(update_tn_win)
  gtk_widget_destroy(update_tn_win);

/* restore vertical position in clist */
gtk_adjustment_set_value(clist_vadj,prev_vadj_value);

/* restart thumbnail-read if needed */
if(was_reading)
  start_thumbnail_read();
}


void recursive_update_internal(GtkWidget **update_tn_win_ptr,
                               GtkWidget *progbar,char *dirname)
{
DIR *dir;
struct dirent *dent;
struct stat sbuf;
int ent;
char *old_cwd;
int f;

/* if they decided to abort or quit completely, give up :-) -
 * the caller will return to the original dir anyway.
 */
if(!mainwin || !*update_tn_win_ptr)
  return;

/* save old cwd to avoid depending on chdir("..") to be sane :-) */
old_cwd=getcwd_allocated();

if(stat(dirname,&sbuf)==-1 || chdir(dirname)==-1)
  {
  free(old_cwd);
  return;
  }

/* see if we've done this one before (a symlink loop could cause this).
 * XXX an array isn't exactly the most efficient thing to search...
 */
for(f=0;f<num_olddirs;f++)
  if(sbuf.st_dev==olddirs[f].device && sbuf.st_ino==olddirs[f].inode)
    {
    chdir(old_cwd);
    free(old_cwd);
    return;
    }

/* save this as a visited dir */
ent=num_olddirs;
num_olddirs++;
olddir_resize_if_needed(num_olddirs);
olddirs[ent].device=sbuf.st_dev;
olddirs[ent].inode=sbuf.st_ino;

/* open the dir */
if((dir=opendir("."))==NULL)
  {
  chdir(old_cwd);
  free(old_cwd);
  return;
  }

/* scan through it and recurse into any dirs */
while((dent=readdir(dir))!=NULL)
  {
  /* skip hidden files (even if we do allow searching these at
   * some point, would want to skip `.'/`..'/`.xvpics').
   */
  if(dent->d_name[0]=='.')
    continue;
  
  /* skip if we can't stat it or it's not a dir */
  if((stat(dent->d_name,&sbuf))==-1 || !S_ISDIR(sbuf.st_mode))
    continue;
  
  /* ok then, recurse. */
  recursive_update_internal(update_tn_win_ptr,progbar,dent->d_name);
  }

closedir(dir);

/* now update thumbnails for this dir */
if(*update_tn_win_ptr && mainwin)
  {
  reinit_dir(0,0);		/* init without pastpos or cursor-pos-save */
  stop_thumbnail_read();
  if(!fast_recursive_update)
    blocking_thumbnail_read_visible(update_tn_win_ptr);
  update_common(update_tn_win_ptr,progbar,1);
  }

/* return to previous dir */
chdir(old_cwd);
free(old_cwd);
}


void cb_update_tn_recursive_confirmed(void)
{
GtkWidget *update_tn_win,*progbar;
GtkAdjustment *clist_vadj;
float prev_vadj_value;
char *origdir;

if(!numrows) return;

/* save orig dir to return to (in case they abort) */
origdir=getcwd_allocated();

/* save vertical position in clist
 * (may not be reasonable once we get back, but IIRC the value is
 * bounds-tested, so that doesn't really matter)
 */
clist_vadj=gtk_clist_get_vadjustment(GTK_CLIST(clist));
prev_vadj_value=clist_vadj->value;

/* also save focus row, via pastpos. */
new_pastpos(GTK_CLIST(clist)->focus_row);

/* remove any running thumbnail read. Once we return to this
 * dir, it'll be scanned from scratch which will restart this.
 */
stop_thumbnail_read();

/* currently we use the normal update win, and do progress
 * for each dir we update. Not great, but easy. :-)
 */
make_update_win(&update_tn_win,&progbar);


/* actually do the recursive update :-) */
olddir_init();
recursive_update_internal(&update_tn_win,progbar,".");
olddir_uninit();


/* if they decided to quit completely, take the hint :-) */
if(!mainwin)
  {
  /* probably unnecessary in practice, but WTF */
  chdir(origdir);
  free(origdir);
  return;
  }

/* if not already destroyed, blast window */
if(update_tn_win)
  gtk_widget_destroy(update_tn_win);

/* return to original dir and rescan it */
chdir(origdir);
free(origdir);
reinit_dir(1,0);	/* init with pastpos */

/* restore vertical position in clist */
gtk_adjustment_set_value(clist_vadj,prev_vadj_value);
}


void cb_update_tn_recursive(void)
{
/* recursive update can take ages; make sure they know. This also explains
 * what recursive actually means, which may help people who don't know.
 * (This is possibly annoying, but I still think it's a good idea.)
 */
confirmation_dialog("Recursive Update",
                    "Recursive updates can take quite some time;\n"
                    "are you sure you want to update this\n"
                    "directory and all its subdirectories?",
                    cb_update_tn_recursive_confirmed);
}
