/* xzgv v0.3 - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks. See main.c for license details.
 *
 * rename.c - file rename dialog.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "backend.h"
#include "main.h"

#include "rename.h"


static GtkWidget *rename_win;
static int current_row;
static char *oldname;


static void cb_ok_button(GtkWidget *button,GtkWidget *entry)
{
struct stat sbuf;
char *tn_src,*tn_dst;
char *dest=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));

gtk_widget_destroy(rename_win);

if(!dest || *dest==0)
  {
  free(dest);
  return;
  }

/* refuse anything with path elements in */
if(strchr(dest,'/'))
  {
  free(dest);
  error_dialog("xzgv error","File must remain in current directory");
  return;
  }

/* refuse the renaming if it would blast an existing file */
if(stat(dest,&sbuf)!=-1)
  {
  free(dest);
  error_dialog("xzgv error","File already exists");
  return;
  }

if(rename(oldname,dest)==-1)
  {
  free(dest);
  error_dialog("xzgv error","Couldn't rename file!");
  return;
  }

/* try renaming any thumbnail, and rename entry when it won't break oldname */
tn_src=tn_dst=NULL;

/* ".xvpics/" is 8 chars */
if((tn_src=malloc(8+strlen(oldname)+1))==NULL ||
   (tn_dst=malloc(8+strlen(dest)+1))==NULL)
  {
  /* rename entry */
  gtk_clist_set_text(GTK_CLIST(clist),current_row,SELECTOR_NAME_COL,dest);
  if(tn_src) free(tn_src);
  resort_finish();
  return;
  }

strcpy(tn_src,".xvpics/");
strcat(tn_src,oldname);
strcpy(tn_dst,".xvpics/");
strcat(tn_dst,dest);

rename(tn_src,tn_dst);		/* don't much care if it works or not */

/* rename entry */
gtk_clist_set_text(GTK_CLIST(clist),current_row,SELECTOR_NAME_COL,dest);

free(tn_dst);
free(tn_src);

free(dest);

resort_finish();
}


void cb_rename_file(void)
{
GtkWidget *vbox;
GtkWidget *action_tbl,*ok_button,*cancel_button;
GtkWidget *table;
GtkWidget *label,*entry;
static char buf[1024];
int tbl_row;

current_row=GTK_CLIST(clist)->focus_row;
if(current_row<0 || current_row>=numrows) return;

oldname=NULL;
gtk_clist_get_text(GTK_CLIST(clist),current_row,SELECTOR_NAME_COL,&oldname);

rename_win=gtk_dialog_new();

gtk_window_set_title(GTK_WINDOW(rename_win),"Rename file");
gtk_window_set_policy(GTK_WINDOW(rename_win),FALSE,TRUE,FALSE);
gtk_window_set_position(GTK_WINDOW(rename_win),GTK_WIN_POS_CENTER);
/* must be modal */
gtk_window_set_modal(GTK_WINDOW(rename_win),TRUE);

/* make a new vbox for the top part so we can get spacing more sane */
vbox=gtk_vbox_new(FALSE,10);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rename_win)->vbox),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(rename_win)->action_area),5);

/* add ok/cancel buttons */
action_tbl=gtk_table_new(1,5,TRUE);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rename_win)->action_area),
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
                          GTK_OBJECT(rename_win));
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

DO_TBL_LEFT(table,tbl_row,"Old filename:");
DO_TBL_RIGHT(table,tbl_row,oldname);
tbl_row++;

DO_TBL_LEFT(table,tbl_row,"New filename:");

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


gtk_widget_show(rename_win);

/* that's it then; it's modal so we can just leave GTK+ to deal with things. */
}
