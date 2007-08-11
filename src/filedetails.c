/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * filedetails.c - code for file details dialog.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "backend.h"
#include "main.h"

#include "filedetails.h"


/* make `rw-r--r--'-style permission string from mode. */
char *make_perms_string(int mode)
{
static char buf[10];
int f,shift,submode;
char *execptr;

strcpy(buf,"---------");

for(f=0,shift=6;f<3;f++,shift-=3)
  {
  /* first do basic `rwx' bit. */
  submode=((mode>>shift)&7);
  if(submode&4) buf[f*3+0]='r';
  if(submode&2) buf[f*3+1]='w';
  if(submode&1) buf[f*3+2]='x';
  
  execptr=buf+f*3+2;
  
  /* apply any setuid/setgid/sticky bits */
  switch(f)
    {
    case 0: if(mode&04000) *execptr=((*execptr=='x')?'s':'S'); break;
    case 1: if(mode&02000) *execptr=((*execptr=='x')?'s':'S'); break;
    case 2: if(mode&01000) *execptr=((*execptr=='x')?'t':'T'); break;
    }
  }

return(buf);
}


/* returns window widget */
GtkWidget *make_details_win(char *filename)
{
GtkWidget *details_win,*vbox;
GtkWidget *action_tbl,*button;
GtkWidget *os_frame,*os_tbl;
GtkWidget *tn_frame,*tn_tbl;
GtkWidget *label;
static char buf[1024];
struct stat sbuf;
struct tm *ctime;
struct passwd *pwptr;
struct group *grptr;
FILE *tn;
int tbl_row;
int got_stat_info=(stat(filename,&sbuf)!=-1);
int got_dimensions=0;
int w,h;
char *ptr;

if((ptr=strrchr(filename,'/'))==NULL)
  {
  strcpy(buf,".xvpics/");
  strcat(buf,filename);
  }
else
  {
  strcpy(buf,filename);
  strcpy(strrchr(buf,'/')+1,".xvpics/");
  strcat(buf,ptr+1);
  }

if((tn=fopen(buf,"rb"))!=NULL)
  {
  fgets(buf,sizeof(buf),tn);	/* lose first line */
  fgets(buf,sizeof(buf),tn);	/* this may be "#IMGINFO:123x456 <type>" */
  while(buf[0]=='#')
    {
    if(sscanf(buf,"#IMGINFO:%dx%d",&w,&h)==2)
      {
      got_dimensions=1;
      break;
      }
    /* otherwise try another comment line */
    fgets(buf,sizeof(buf),tn);
    }
  fclose(tn);
  }


details_win=gtk_dialog_new();

gtk_window_set_title(GTK_WINDOW(details_win),"File Details");
gtk_window_set_policy(GTK_WINDOW(details_win),FALSE,TRUE,FALSE);
gtk_window_set_position(GTK_WINDOW(details_win),GTK_WIN_POS_CENTER);
/* it's kludgey making it modal, but it does simplify things... */
gtk_window_set_modal(GTK_WINDOW(details_win),TRUE);

/* make a new vbox for the top part so we can get spacing more sane */
vbox=gtk_vbox_new(FALSE,10);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(details_win)->vbox),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(details_win)->action_area),5);

/* add ok button */
action_tbl=gtk_table_new(1,5,TRUE);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(details_win)->action_area),
                   action_tbl,TRUE,TRUE,0);
gtk_widget_show(action_tbl);

button=gtk_button_new_with_label("Ok");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),button,2,3,0,1);
gtk_signal_connect_object(GTK_OBJECT(button),"clicked",
                          GTK_SIGNAL_FUNC(gtk_widget_destroy),
                          GTK_OBJECT(details_win));
gtk_widget_grab_focus(button);
gtk_widget_show(button);

/* add file details */
os_frame=gtk_frame_new("Details from OS");
gtk_box_pack_start(GTK_BOX(vbox),os_frame,TRUE,TRUE,0);
gtk_widget_show(os_frame);

/* 3 columns used to get the proportions nice - left one is for
 * description (e.g. "Filename:"), right two for value (e.g. "foo.jpg").
 */
os_tbl=gtk_table_new(6,3,TRUE);
gtk_container_add(GTK_CONTAINER(os_frame),os_tbl);
gtk_table_set_col_spacings(GTK_TABLE(os_tbl),20);
gtk_table_set_row_spacings(GTK_TABLE(os_tbl),8);
gtk_container_set_border_width(GTK_CONTAINER(os_tbl),8);
gtk_widget_show(os_tbl);

/* couldn't get gtk_label_set_justify() to work (perhaps this is
 * only relevant to multi-line labels?), but gtk_misc_set_alignment() is ok.
 */
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

/* some of the info below (size, mtime) we have in the data ptr too,
 * but we need the stat() result for permissions, so we might as well
 * use it for the lot.
 */

tbl_row=0;

DO_TBL_LEFT(os_tbl,tbl_row,"Filename:");
DO_TBL_RIGHT(os_tbl,tbl_row,filename);
tbl_row++;

strcpy(buf,"unknown");

DO_TBL_LEFT(os_tbl,tbl_row,"Size:");
if(got_stat_info)
  sprintf(buf,"%d  (%dk)",(int)sbuf.st_size,((int)sbuf.st_size+1023)/1024);
DO_TBL_RIGHT(os_tbl,tbl_row,buf);
tbl_row++;

DO_TBL_LEFT(os_tbl,tbl_row,"Last modified:");
if(got_stat_info)
  {
  if((ctime=localtime(&sbuf.st_mtime))==NULL)	/* can't happen? */
    strcpy(buf,"unknown");
  else
    sprintf(buf,"%d-%02d-%02d  %02d:%02d",
            1900+ctime->tm_year,ctime->tm_mon+1,ctime->tm_mday,
            ctime->tm_hour,ctime->tm_min);
  }
DO_TBL_RIGHT(os_tbl,tbl_row,buf);
tbl_row++;

DO_TBL_LEFT(os_tbl,tbl_row,"Permissions:");
if(got_stat_info)
  sprintf(buf,"%s  (%o)",
          make_perms_string(sbuf.st_mode&07777),sbuf.st_mode&07777);
DO_TBL_RIGHT(os_tbl,tbl_row,buf);
tbl_row++;

DO_TBL_LEFT(os_tbl,tbl_row,"Owner:");
pwptr=NULL;
if(got_stat_info)
  pwptr=getpwuid(sbuf.st_uid);
DO_TBL_RIGHT(os_tbl,tbl_row,pwptr?pwptr->pw_name:"unknown");
tbl_row++;

DO_TBL_LEFT(os_tbl,tbl_row,"Group:");
grptr=NULL;
if(got_stat_info)
  grptr=getgrgid(sbuf.st_gid);
DO_TBL_RIGHT(os_tbl,tbl_row,grptr?grptr->gr_name:"unknown");
tbl_row++;


tn_frame=gtk_frame_new("Details from thumbnail");
gtk_box_pack_start(GTK_BOX(vbox),tn_frame,TRUE,TRUE,0);
gtk_widget_show(tn_frame);

tn_tbl=gtk_table_new(1,3,TRUE);
gtk_container_add(GTK_CONTAINER(tn_frame),tn_tbl);
gtk_table_set_col_spacings(GTK_TABLE(tn_tbl),20);
gtk_table_set_row_spacings(GTK_TABLE(tn_tbl),8);
gtk_container_set_border_width(GTK_CONTAINER(tn_tbl),8);
gtk_widget_show(tn_tbl);

tbl_row=0;

DO_TBL_LEFT(tn_tbl,tbl_row,"Dimensions:");
if(got_dimensions)
  sprintf(buf,"%d x %d",w,h);
else
  strcpy(buf,"unknown");
DO_TBL_RIGHT(tn_tbl,tbl_row,buf);
tbl_row++;

if(!got_dimensions)
  gtk_widget_set_sensitive(tn_frame,FALSE);


/* esc also acks it (even from main window!) */
gtk_widget_add_accelerator(button,"clicked",gtk_accel_group_get_default(),
                           GDK_Escape,0,0);


gtk_widget_show(details_win);

return(details_win);
}


void cb_file_details(void)
{
GtkWidget *details_win;
char *ptr;
int row;

/* if focus row is somehow bogus, don't do it */
row=GTK_CLIST(clist)->focus_row;
if(row<0 || row>=numrows)
  return;

/* make the window and fill with details */
gtk_clist_get_text(GTK_CLIST(clist),row,SELECTOR_NAME_COL,&ptr);
details_win=make_details_win(ptr);

/* that's it then; it's modal so we can just leave GTK+ to deal with things. */
}
