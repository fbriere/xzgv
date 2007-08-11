/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * gotodir.c - `go to dir' dialog.
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

#include "gotodir.h"


static GtkWidget *dir_win;



static void cb_ok_button(GtkWidget *button,GtkWidget *entry)
{
char *ptr=gtk_entry_get_text(GTK_ENTRY(entry));
int ret;

if(!ptr || *ptr==0 || strcmp(ptr,".")==0)
  {
  gtk_widget_destroy(dir_win);
  return;
  }

if(*ptr=='~' && getenv("HOME"))		/* kludge for home dir */
  ptr=g_strdup_printf("%s%s",getenv("HOME"),ptr+1);
else
  ptr=g_strdup(ptr);	/* not needed but easier this way :-) */

ret=chdir(ptr);

free(ptr);
gtk_widget_destroy(dir_win);

if(ret==0)
  {
  stop_thumbnail_read();
  do_gtk_stuff(); /* sometimes there's a delay before redraw otherwise */
  reinit_dir(1,0);
  }
else
  error_dialog("xzgv error","Couldn't change directory!");
}


void cb_goto_dir(void)
{
GtkWidget *vbox;
GtkWidget *action_tbl,*ok_button,*cancel_button;
GtkWidget *table;
GtkWidget *label,*entry;
static char cdir[1024],buf[1024];
int tbl_row;

getcwd(cdir,sizeof(cdir)-1);

dir_win=gtk_dialog_new();

gtk_window_set_title(GTK_WINDOW(dir_win),"Go to directory");
gtk_window_set_policy(GTK_WINDOW(dir_win),FALSE,TRUE,FALSE);
gtk_window_set_position(GTK_WINDOW(dir_win),GTK_WIN_POS_CENTER);
/* seems reasonable to have this as modal */
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
                          GTK_SIGNAL_FUNC(gtk_widget_destroy),GTK_OBJECT(dir_win));
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

DO_TBL_LEFT(table,tbl_row,"New dir:");

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
