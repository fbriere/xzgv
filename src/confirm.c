/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * confirm.c - a generic yes/no confirmation dialog.
 */

#include <stdio.h>
#include <sys/types.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "backend.h"
#include "main.h"

#include "confirm.h"


static GtkWidget *yesno_win;



static void cb_yes_button(GtkWidget *button,void (*callback)(void))
{
gtk_widget_destroy(yesno_win);
(*callback)();
}


/* a yes/no confirmation dialog. You specify the title, message text,
 * and the main callback for the `yes' button (the `no' does nothing
 * other than removing the yes/no dialog).
 *
 * Note that this returns `immediately'; the callback should do
 * any work required.
 */
void confirmation_dialog(char *title,char *msg,void (*main_callback)(void))
{
GtkWidget *vbox;
GtkWidget *action_tbl,*yes_button,*no_button;
GtkWidget *label;

yesno_win=gtk_dialog_new();

gtk_window_set_title(GTK_WINDOW(yesno_win),title);
gtk_window_set_policy(GTK_WINDOW(yesno_win),FALSE,TRUE,FALSE);
gtk_window_set_position(GTK_WINDOW(yesno_win),GTK_WIN_POS_CENTER);
/* seems reasonable to have this as modal */
gtk_window_set_modal(GTK_WINDOW(yesno_win),TRUE);

/* make a new vbox for the top part so we can get spacing more sane */
vbox=gtk_vbox_new(FALSE,10);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(yesno_win)->vbox),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(yesno_win)->action_area),5);

/* add yes/no buttons */
action_tbl=gtk_table_new(1,5,TRUE);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(yesno_win)->action_area),
                   action_tbl,TRUE,TRUE,0);
gtk_widget_show(action_tbl);

yes_button=gtk_button_new_with_label("Yes");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),yes_button, 1,2, 0,1);
gtk_signal_connect(GTK_OBJECT(yes_button),"clicked",
                   GTK_SIGNAL_FUNC(cb_yes_button),main_callback);
gtk_widget_show(yes_button);

no_button=gtk_button_new_with_label("No");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),no_button, 3,4, 0,1);
gtk_signal_connect_object(GTK_OBJECT(no_button),"clicked",
                          GTK_SIGNAL_FUNC(gtk_widget_destroy),
                          GTK_OBJECT(yesno_win));
gtk_widget_show(no_button);


label=gtk_label_new(msg);
gtk_box_pack_start(GTK_BOX(vbox),label,TRUE,TRUE,2);
gtk_widget_show(label);


/* esc/n = no */
gtk_widget_add_accelerator(no_button,"clicked",
                           gtk_accel_group_get_default(),
                           GDK_Escape,0,0);
gtk_widget_add_accelerator(no_button,"clicked",
                           gtk_accel_group_get_default(),
                           GDK_n,0,0);

/* enter/y = yes */
gtk_widget_add_accelerator(yes_button,"clicked",
                           gtk_accel_group_get_default(),
                           GDK_Return,0,0);
gtk_widget_add_accelerator(yes_button,"clicked",
                           gtk_accel_group_get_default(),
                           GDK_y,0,0);


gtk_widget_show(yesno_win);

/* that's it then; it's modal so we can just leave GTK+ to deal with things. */
}
