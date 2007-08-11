/* xzgv v0.3 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * copymove.c - code for file copy/move.
 *
 * Based on both gotodir.c and zgv's copymove.c.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "backend.h"
#include "main.h"

#include "copymove.h"


#define TRANSFER_BUF_SIZE	8192


static GtkWidget *dir_win;
static int cm_do_move;



/* ------ actual copy/move file code ------ */


static int cm_isdir(char *filename)
{
struct stat sbuf;

return(stat(filename,&sbuf)!=-1 && S_ISDIR(sbuf.st_mode));
}


/* copy file, returns 0 if failed
 * src must be in the current directory (though this isn't checked)
 * and dstdir must be a directory (fails if it isn't)
 */
int copyfile(char *src,char *dstdir)
{
static unsigned char copybuf[TRANSFER_BUF_SIZE];
FILE *in,*out;
char *dst;
int siz;

if(!cm_isdir(dstdir)) return(0);

if((in=fopen(src,"rb"))==NULL)
  return(0);

if((dst=malloc(strlen(dstdir)+strlen(src)+2))==NULL)	/* +2 for / and NUL */
  return(0);

strcpy(dst,dstdir);
strcat(dst,"/");
if(strrchr(src,'/'))
  strcat(dst,strrchr(src,'/')+1);
else
  strcat(dst,src);

/* check it doesn't already exist */
if((out=fopen(dst,"rb"))!=NULL)
  {
  fclose(out);
  fclose(in);
  free(dst);
  return(0);
  }

if((out=fopen(dst,"wb"))==NULL)
  {
  fclose(in);
  free(dst);
  return(0);
  }

free(dst);

/* so now both files are open.
 * copy from file to file, up to TRANSFER_BUF_SIZE bytes at a time.
 */

while((siz=fread(copybuf,1,TRANSFER_BUF_SIZE,in))>0)
  {
  if(fwrite(copybuf,1,siz,out)!=siz)
    {
    fclose(out);
    fclose(in);
    return(0);
    }
  }

fclose(out);
fclose(in);
return(1);
}


/* move file, returns 0 if failed
 * src must be in the current directory (though this isn't checked)
 * and dstdir must be a directory (fails if it isn't)
 */
int movefile(char *src,char *dstdir)
{
struct stat sbuf;
struct utimbuf utbuf;
char *dst;

if(!cm_isdir(dstdir)) return(0);

if((dst=malloc(strlen(dstdir)+strlen(src)+2))==NULL)	/* +2 for / and NUL */
  return(0);

strcpy(dst,dstdir);
strcat(dst,"/");
if(strrchr(src,'/'))
  strcat(dst,strrchr(src,'/')+1);
else
  strcat(dst,src);

/* fail if dest file already exists */
if(stat(dst,&sbuf)==0)
  {
  free(dst);
  return(0);
  }

/* first try a rename() */
if(rename(src,dst)==0)
  {
  /* it worked */
  free(dst);
  return(1);
  }

if(errno!=EXDEV)
  {
  /* if the reason it failed wasn't because it couldn't `rename'
   * between filesystems, we should fail too.
   */
  free(dst);
  return(0);
  }

/* resort to copying then deleting */

if(!copyfile(src,dstdir))
  {
  free(dst);
  return(0);
  }

/* try to copy file times/owner/group/perms.
 * I don't consider this important enough to give an error if
 * it screws up (esp. 'cos setting owner might in some cases screw up
 * quite reasonably if we're not root :-)), but it's worth doing I
 * think - not least for consistency with the case where rename() works
 * (where everything is preserved).
 */
if(stat(src,&sbuf)==0)
  {
  /* set times */
  utbuf.actime=sbuf.st_atime;
  utbuf.modtime=sbuf.st_mtime;
  utime(dst,&utbuf);	/* don't much care if it fails */
  
  /* set owner/group and perms - these don't change the times set above */
  chown(dst,sbuf.st_uid,sbuf.st_gid);
  chmod(dst,sbuf.st_mode);
  /* again, not too bothered if they fail */
  }

/* the copy worked, delete the original */
if(remove(src)==0)
  {
  /* the delete worked, all done */
  free(dst);
  return(1);
  }

/* if we couldn't delete the original, delete the copy instead,
 * because the overall move failed and it would be messy to leave
 * it there. However, we're not really that bothered if *this*
 * delete fails. :-)
 */
remove(dst);

free(dst);
return(0);
}




/* ------ UI code ------ */


/* see if we have either tagged files, or cursor on a file (not a dir). */
int ok_for_copymove(void)
{
struct clist_data_tag *datptr;
int f,t;

if(!numrows) return(0);

for(f=t=0;f<numrows;f++)
  if(get_tagged_state(f))
    t++;

datptr=gtk_clist_get_row_data(GTK_CLIST(clist),GTK_CLIST(clist)->focus_row);

return(t || !datptr->isdir);
}



/* returns window widget, and progbar in arg */
static void make_progress_win(GtkWidget **progress_win_ret,
                              GtkWidget **progbar_ret)
{
GtkWidget *progress_win;
GtkWidget *progbar,*button;

progress_win=*progress_win_ret=gtk_dialog_new();

/* set returned pointer to NULL when destroyed */
gtk_signal_connect(GTK_OBJECT(progress_win),"destroy",
                   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
                   progress_win_ret);

gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(progress_win)->vbox),2);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(progress_win)->action_area),0);

gtk_window_set_title(GTK_WINDOW(progress_win),
                     cm_do_move?"Moving":"Copying");
gtk_window_set_policy(GTK_WINDOW(progress_win),FALSE,TRUE,FALSE);
gtk_widget_set_usize(progress_win,250,55);
gtk_window_set_position(GTK_WINDOW(progress_win),GTK_WIN_POS_CENTER);
gtk_window_set_modal(GTK_WINDOW(progress_win),TRUE);

progbar=*progbar_ret=gtk_progress_bar_new();
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_win)->vbox),
                   progbar,TRUE,TRUE,2);
gtk_widget_show(progbar);

button=gtk_button_new_with_label("Cancel");
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_win)->action_area),
                   button,TRUE,TRUE,2);
gtk_signal_connect_object(GTK_OBJECT(button),"clicked",
                          GTK_SIGNAL_FUNC(gtk_widget_destroy),
                          GTK_OBJECT(progress_win));
gtk_widget_grab_focus(button);
gtk_widget_show(button);

/* esc also aborts (even from main window!) */
gtk_widget_add_accelerator(button,"clicked",gtk_accel_group_get_default(),
                           GDK_Escape,0,0);


gtk_widget_show(progress_win);
}


/* do the copy/moves, showing how far we've got. */
void cm_copymove_gotdir(char *destdir)
{
static char buf[256];
GtkWidget *progress_win,*progbar;
int (*copy_or_move_ptr)(char *,char *);
int f,t,numtagged;
int done;
char *ptr;

/* if by some miracle there are no files, don't bother ;-) */
if(!numrows) return;

/* stop any running thumbnail read. The caller rescans the dir after
 * we're done anyway.
 */
if(thumbnail_read_running())
  stop_thumbnail_read();

copy_or_move_ptr=cm_do_move?movefile:copyfile;

for(f=t=0;f<numrows;f++)
  if(get_tagged_state(f))
    t++;

numtagged=t;
if(t==0) numtagged=1;	/* used for progress bar, so fake it :-) */

make_progress_win(&progress_win,&progbar);

if(progress_win && mainwin)
  gtk_progress_configure(GTK_PROGRESS(progbar),0.,0.,(float)numtagged);

/* do GTK+ update early */
if(progress_win && mainwin)
  do_gtk_stuff();

for(done=f=0;f<numrows;f++)
  {
  /* kludge for cursor file
   * may lead one to think the code
   * sucks in untold ways.
   *
   * "For example, er... where is Eric Cartman?"
   * "That's a haiku?"
   * 		-- South Park
   */
  if(t==0)
    f=GTK_CLIST(clist)->focus_row;
  else
    if(!get_tagged_state(f)) continue;
  
  gtk_clist_get_text(GTK_CLIST(clist),f,SELECTOR_NAME_COL,&ptr);
  
  if(!(*copy_or_move_ptr)(ptr,destdir))
    {
    sprintf(buf,"Error %s ",cm_do_move?"moving":"copying");
    /* if it's a really big filename just say "file" :-) */
    strcat(buf,(strlen(ptr)>100)?"file":ptr);
    
    if(mainwin)
      {
      if(progress_win)
        gtk_widget_destroy(progress_win);
      
      error_dialog("xzgv error",buf);
      }
    
    return;
    }
  
  done++;
  
  /* update progress bar, and give it a chance to draw */
  if(progress_win && mainwin)
    {
    gtk_progress_set_value(GTK_PROGRESS(progbar),(float)done);
    do_gtk_stuff();
    }
  
  if(!progress_win || !mainwin)
    break;		/* they must have aborted */

  if(t==0) break;
  }

/* if they decided to quit completely, take the hint :-) */
if(!mainwin)
  return;

/* if not already destroyed, blast window */
if(progress_win)
  gtk_widget_destroy(progress_win);
}


/* XXX much of the following is the same as gotodir.c. Should merge
 * these together if reasonable.
 */

static void cb_ok_button(GtkWidget *button,GtkWidget *entry)
{
char *ptr=gtk_entry_get_text(GTK_ENTRY(entry));
int isdir;

if(!ptr || *ptr==0 || strcmp(ptr,".")==0)
  {
  gtk_widget_destroy(dir_win);
  return;
  }

if(*ptr=='~' && getenv("HOME"))		/* kludge for home dir */
  ptr=g_strdup_printf("%s%s",getenv("HOME"),ptr+1);
else
  ptr=g_strdup(ptr);	/* not needed but easier this way :-) */

isdir=cm_isdir(ptr);

/* now do the actual copy/move */
if(isdir)
  cm_copymove_gotdir(ptr);

free(ptr);
gtk_widget_destroy(dir_win);

if(!isdir)
  {
  error_dialog("xzgv error","Directory not found!");
  return;
  }

if(!cmdline_files)	/* don't update if run as `xzgv file(s)' */
  {
  /* unpredictable what'll happen to files in current dir; safest to
   * just always update. (Yes, even if copying, as we stopped thumbnail
   * update in cm_copymove_gotdir().) However, save pastpos first, so
   * we stand a decent chance of staying at a tolerably similar place
   * in the dir.
   */
  new_pastpos(GTK_CLIST(clist)->focus_row);

  reinit_dir(1,0);
  }
}


void cb_copymove_file_or_tagged_files(int do_move)
{
GtkWidget *vbox;
GtkWidget *action_tbl,*ok_button,*cancel_button;
GtkWidget *table;
GtkWidget *label,*entry;
static char cdir[1024],buf[1024];
int tbl_row;

if(!ok_for_copymove())
  {
  error_dialog("xzgv error","No files tagged and cursor not on a file");
  return;
  }

cm_do_move=do_move;		/* save for later */

getcwd(cdir,sizeof(cdir)-1);

dir_win=gtk_dialog_new();

gtk_window_set_title(GTK_WINDOW(dir_win),
                     do_move?"Move file(s) to dir":"Copy file(s) to dir");
gtk_window_set_policy(GTK_WINDOW(dir_win),FALSE,TRUE,FALSE);
gtk_window_set_position(GTK_WINDOW(dir_win),GTK_WIN_POS_CENTER);

/* must be modal given the way we save static data like cm_do_move */
gtk_window_set_modal(GTK_WINDOW(dir_win),TRUE);

/* make a new vbox for the top part so we can get spacing more sane */
vbox=gtk_vbox_new(FALSE,10);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dir_win)->vbox),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(dir_win)->action_area),5);

/* add ok/cancel buttons */
action_tbl=gtk_table_new(1,5,TRUE);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dir_win)->action_area),
                   action_tbl,TRUE,TRUE,0);
gtk_widget_show(action_tbl);

ok_button=gtk_button_new_with_label("Ok");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),ok_button, 1,2, 0,1);
/* we connect the signal later, so we can refer to the text-entry */
gtk_widget_show(ok_button);

cancel_button=gtk_button_new_with_label("Cancel");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),cancel_button, 3,4, 0,1);
gtk_signal_connect_object(GTK_OBJECT(cancel_button),"clicked",
                          GTK_SIGNAL_FUNC(gtk_widget_destroy),
                          GTK_OBJECT(dir_win));
gtk_widget_show(cancel_button);


table=gtk_table_new(2,3,TRUE);
gtk_box_pack_start(GTK_BOX(vbox),table,TRUE,TRUE,0);
gtk_table_set_col_spacings(GTK_TABLE(table),20);
gtk_table_set_row_spacings(GTK_TABLE(table),8);
gtk_container_set_border_width(GTK_CONTAINER(table),8);
gtk_widget_show(table);

#define DO_TBL_LEFT(table,row,name) \
  label=gtk_label_new(name);				\
  gtk_misc_set_alignment(GTK_MISC(label),1.,0.5);	\
  gtk_table_attach_defaults(GTK_TABLE(table),label, 0,1, (row),(row)+1); \
  gtk_widget_show(label)

#define DO_TBL_RIGHT(table,row,name) \
  label=gtk_label_new(name);				\
  gtk_misc_set_alignment(GTK_MISC(label),0.,0.5);	\
  gtk_table_attach_defaults(GTK_TABLE(table),label, 1,3, (row),(row)+1); \
  gtk_widget_show(label)

tbl_row=0;

DO_TBL_LEFT(table,tbl_row,"Current dir:");
DO_TBL_RIGHT(table,tbl_row,cdir);
tbl_row++;

DO_TBL_LEFT(table,tbl_row,"Target dir:");

entry=gtk_entry_new_with_max_length(sizeof(buf)-1);
gtk_table_attach_defaults(GTK_TABLE(table),entry, 1,3, tbl_row,tbl_row+1);
gtk_widget_grab_focus(entry);
gtk_widget_show(entry);
gtk_signal_connect(GTK_OBJECT(entry),"activate",
                   GTK_SIGNAL_FUNC(cb_ok_button),GTK_OBJECT(entry));

/* finally connect up the ok button */
gtk_signal_connect(GTK_OBJECT(ok_button),"clicked",
                   GTK_SIGNAL_FUNC(cb_ok_button),GTK_OBJECT(entry));


/* esc = cancel */
gtk_widget_add_accelerator(cancel_button,"clicked",
                           gtk_accel_group_get_default(),
                           GDK_Escape,0,0);

gtk_widget_add_accelerator(ok_button,"clicked",
                           gtk_accel_group_get_default(),
                           GDK_Return,0,0);


gtk_widget_show(dir_win);

/* that's it then; it's modal so we can just leave GTK+ to deal with things. */
}
