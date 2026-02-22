/* xzgv - picture viewer for X, with file selector.
 * Copyright (C) 1999-2003 Russell Marks. See main.c for license details.
 *
 * help.c - help viewing, and about box.
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
#include "rcfile.h"	/* for XZGV_VER */

#include "help.h"


void help_about(void)
{
GtkWidget *about_win;
GtkWidget *vbox,*label,*action_tbl,*button;

about_win=gtk_dialog_new();

/* make a new vbox for the top part so we can get spacing more sane */
vbox=gtk_vbox_new(FALSE,10);
gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(about_win))),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(about_win))),2);

gtk_window_set_title(GTK_WINDOW(about_win),"About xzgv");
gtk_window_set_resizable(GTK_WINDOW(about_win),TRUE);
gtk_window_set_position(GTK_WINDOW(about_win),GTK_WIN_POS_CENTER);
/* doesn't really need to be modal, but it'd be confusing if it weren't */
gtk_window_set_modal(GTK_WINDOW(about_win),TRUE);

label=gtk_label_new("xzgv " XZGV_VER " - picture viewer for X with file selector\n"
                    "Copyright (C) 1999-2005 Russell Marks\n"
                    "Copyright (C) 2007 Reuben Thomas\n"
                    "Homepage - http://sourceforge.net/projects/xzgv");
gtk_box_pack_start(GTK_BOX(vbox),label,TRUE,TRUE,2);
gtk_widget_show(label);

/* add ok button */
action_tbl=gtk_table_new(1,3,TRUE);
gtk_box_pack_start(GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(about_win))),
                   action_tbl,TRUE,TRUE,0);
gtk_widget_show(action_tbl);

button=gtk_button_new_with_label("Ok");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),button, 1,2, 0,1);
g_signal_connect_swapped(button,"clicked",
                          G_CALLBACK(gtk_widget_destroy),
                          about_win);
gtk_widget_grab_focus(button);
gtk_widget_show(button);

/* also allow escs (even from main window!) */
gtk_widget_add_accelerator(button,"clicked",mainwin_accel_group,
                           GDK_KEY_Escape,0,0);


gtk_widget_show(about_win);
}


/* currently, this runs info in an xterm on the specified node.
 * It's better than nothing, but it wouldn't hurt to have something
 * nicer... :-)
 *
 * And don't worry, this *will* be configurable in future (XXX).
 */
void help_run(char *node)
{
char *cmd_start="xterm -e info '(xzgv)";
char *cmd_end="' &";
char *buf;

if((buf=malloc(strlen(cmd_start)+strlen(node)+strlen(cmd_end)+1))==NULL)
  {
  /* if we're *that* low on memory, then error_dialog() will fail too,
   * so just return.
   */
  return;
  }

strcpy(buf,cmd_start);
strcat(buf,node);
strcat(buf,cmd_end);

/* XXX it turns out the error check is useless, as the `&' leads to
 * starting another shell which is the one to give any errors. The
 * one we directly run here is always happy, error or not. :-/
 * Looks like I'll need the full fork/exec junk, oh well...
 */
if(system(buf)!=0)
  {
  char *msg="Couldn't run help command:\n";
  char *buf2;
  
  if((buf2=malloc(strlen(msg)+strlen(buf)+1))==NULL)
    error_dialog("xzgv error",msg);
  else
    {
    strcpy(buf2,msg);
    strcat(buf2,buf);
    error_dialog("xzgv error",buf2);
    free(buf2);
    }

  free(buf);
  return;
  }

free(buf);
}
