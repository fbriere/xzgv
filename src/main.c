/* xzgv - picture viewer for X, with file selector.
 * Copyright (C) 1999-2003 Russell Marks.
 * Copyright (C) 2007 Reuben Thomas.
 *
 * main.c - the guts of the program (selector, viewer, etc.).
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* XXX there's really too much stuff here, much of it could/should
 * be moved out to other files...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>		/* needed for iconify stuff */
#include <X11/Xlib.h>		/* needed for iconify stuff */

#include "backend.h"
#include "resizepic.h"
#include "rcfile.h"		/* needed for config vars */
#include "filedetails.h"
#include "gotodir.h"
#include "updatetn.h"
#include "confirm.h"
#include "misc.h"
#include "copymove.h"
#include "rename.h"
#include "help.h"

#include "dir_icon.xpm"
#include "dir_icon_small.xpm"
#include "file_icon.xpm"
#include "file_icon_small.xpm"
#include "icon-48.xpm"

#include "main.h"


/* number of thumbnails idle_xvpic_load() attempts to load per call.
 * 1 is a little on the small side :-), but should keep it tolerably
 * interactive while loading thumbnails on slower machines (I hope!).
 */
#define IDLE_XVPIC_NUM_PER_CALL		1

/* row heights - normal and `thin'. I wouldn't mess about with these
 * unless you have a really good reason to. :-)
 */
#define ROW_HEIGHT_NORMAL	(60+2)
#define ROW_HEIGHT_DIV		3
#define ROW_HEIGHT_THIN		(20+2)

/* maximum no. of `past positions' in dirs to save.
 * if it runs out of space the oldest entries are lost.
 */
#define MAX_PASTPOS	256

/* limit on scaling down - entirely arbitrary */
#define SCALING_DOWN_LIMIT	(-32)

/* for defence against render_pixbuf recursive callbacks, etc.
 * Be sure to do RECURSE_PROTECT_END before *any* possible exit
 * (but as late as possible, of course).
 */
#define RECURSE_PROTECT_START	static int here=0; if(here) return; here=1
#define RECURSE_PROTECT_END	here=0

/* dragging gestures were added in GTK 3.14 */
#define HAVE_DRAG_GESTURES GTK_CHECK_VERSION(3,14,0)


GtkWidget *align,*sw_for_pic;
GtkWidget *image_widget, *eb_for_pic;
GtkWidget *treeview,*statusbar,*sw_for_flist,*flist_sw_ebox;
GtkWidget *selector_menu,*viewer_menu;
GtkWidget *zoom_widget;		/* widget for zoom opt on menu */
GtkWidget *pane;
guint sel_id;			/* selector id for statusbar messages */
guint tn_id;			/* `thumbnail' id for statusbar messages */
int focus_row = -1;

GtkWidget *mainwin;

GtkListStore *liststore;
GtkCellRenderer *thumbnail_renderer;  /* thumbnail cell renderer, for toggle_thin_row() */

guint8 xvpic_pal[256][3];		/* palette for thumbnails */

/* image & rendered pixbuf for currently-loaded image */
xzgv_image *theimage=NULL;
GdkPixbuf *thepixbuf=NULL;

/* no-thumbnail icon pixbufs */
GdkPixbuf *dir_icon,*file_icon;
GdkPixbuf *dir_icon_small,*file_icon_small;

/* stuff for the idle-func thumbnail loading */
gint tn_idle_tag=-1;		/* tag returned by g_idle_add() */
float idle_xvpic_lastadjval;
int idle_xvpic_jumped=0;	/* true if xvpic load jumped ahead */
int idle_xvpic_blocked=0;	/* disables idle_xvpic_load() temporarily */
int idle_xvpic_called=0;	/* set when idle_xvpic_load is called */
int idle_xvpic_entry_idle;	/* entry placeholder when using g_idle_add() */

int numrows=0;			/* number of rows in liststore */

gint zoom_resize_idle_tag=-1;	/* tag for zoom-resize kludge idle func */

int listen_to_toggles=0;	/* ignore fix-up toggles initially */
				/* (see init_window()) */
int in_nextprev=0;		/* needed to protect against recursion */

int orig_x,orig_y;		/* for image dragging with mouse */
int next_on_release=0;		/* if true, do next-pic on but1 release */
int current_selection=-1;	/* needed for viewer's next/previous file */
guint cb_selection_id;		/* id of cb_selection() handler */
int ignore_selector_input=0;	/* awkward but necessary, for blocking input */
int hide_saved_pos;		/* saved pane-split pos for auto-hide */
int hidden=0;			/* selector hidden if true */
int orient_current_state=0;	/* current picture orientation state */
int jpeg_exif_orient=0;		/* orientation from Exif tag, for some JPEGs */

int cmdline_files=0;		/* if true, started as `xzgv file(s)' */

int xscaling=1,yscaling=1;

/* GTK+ border thickness in scrolled window (not counting scrollbars). */
int sw_border_width,sw_border_height;



struct pastpos_tag
  {
  int dev,inode,row;
  } pastpos[MAX_PASTPOS];


/* Scary orientation stuff
 * -----------------------
 *
 * There are eight possible orientations (0 is the original image):
 *                             _____     _____ 
 *    _______     _______     |    a|   |    b|
 *   |a      |   |b      |    |     |   |     |
 *   |   0   |   |   1   |    |  4  |   |  5  |
 *   |______b|   |______a|    |b____|   |a____|
 *    _______     _______      _____     _____ 
 *   |      b|   |      a|    |b    |   |a    |
 *   |   2   |   |   3   |    |     |   |     |
 *   |a______|   |b______|    |  6  |   |  7  |
 *                            |____a|   |____b|
 *
 * That gives us these changes in orientation state for each of the
 * orientation-changing operations (rotate, mirror, flip):
 *
 * 		rot-cw	rot-acw	mirror	flip
 * 0 to...	4	5	3	2
 * 1 to...	5	4	2	3
 * 2 to...	7	6	1	0
 * 3 to...	6	7	0	1
 * 4 to...	1	0	7	6
 * 5 to...	0	1	6	7
 * 6 to...	2	3	5	4
 * 7 to...	3	2	4	5
 */

int orient_state_rot_cw[8] ={4,5,7,6,1,0,2,3};
int orient_state_rot_acw[8]={5,4,6,7,0,1,3,2};
int orient_state_mirror[8] ={3,2,1,0,7,6,5,4};
int orient_state_flip[8]   ={2,3,0,1,6,7,4,5};



/* required prototypes */
void render_pixbuf(int reset_pos);
void cb_nextprev_tagged_image(int next,int view);
gint idle_xvpic_load(int *entryp);
gint pic_win_resized(GtkWidget *widget,GdkEventConfigure *event);
void cb_scaling_double(void);
void cb_xscaling_double(void);
void cb_yscaling_double(void);
void cb_scaling_halve(void);
void cb_xscaling_halve(void);
void cb_yscaling_halve(void);
void cb_next_image(void);
void cb_tag_then_next(void);
void set_title(int include_dir);
void set_window_pos_and_size(void);




void swap_xyscaling(void)
{
int tmp=xscaling;

xscaling=yscaling;
yscaling=tmp;
}


/* change from one orientation state to another.
 * (See the comment about this above.)
 */
void orient_change_state(int from,int to)
{
/* the basic idea is this:
 *
 * - if from and to are equal, return.
 * - if a single flip/mirror/rot will do it, use that.
 * - otherwise, try a rotate if we know it's needed (see below).
 * - then see if a flip/mirror does the trick.
 * - if not, it must need flip *and* mirror.
 */
int state=from;

if(from==to) return;

#define DO_FLIP		backend_flip_vert(theimage)
#define DO_MIRROR	backend_flip_horiz(theimage)
#define DO_ROT_CW	backend_rotate_cw(theimage), \
			swap_xyscaling()

/* try a one-step route. */
if(orient_state_flip[state]==to)	{ DO_FLIP; return; }
if(orient_state_mirror[state]==to)	{ DO_MIRROR; return; }
if(orient_state_rot_cw[state]==to)
  {
  DO_ROT_CW;
  return;
  }

/* nope, ok then, things get complicated.
 * we can get any required rotate out of the way -
 * if it's switched from portrait to landscape or vice versa, we must
 * need one. That's if it's gone from 0..3 to 4..7 or 4..7 to 3..0.
 */
if((from<4 && to>=4) || (from>=4 && to<4))
  {
  DO_ROT_CW;
  state=orient_state_rot_cw[state];
  }

/* now try a flip/mirror. */
if(orient_state_flip[state]==to)	{ DO_FLIP; return; }
if(orient_state_mirror[state]==to)	{ DO_MIRROR; return; }

/* no? Well it must need both then. */
DO_FLIP;
DO_MIRROR;

/* sanity check */
if(orient_state_mirror[orient_state_flip[state]]!=to)
  fprintf(stderr,"can't happen - orient_change_state(%d,%d) failed!\n",
          from,to);
}


/* run GTK+ stuff until events are dealt with. Normally the idle func
 * to load thumbnails, if running, would take this opportunity to
 * completely finish loading the thumbnails. So we disable that
 * temporarily.
 */
void do_gtk_stuff(void)
{
idle_xvpic_blocked=1;
idle_xvpic_called=0;

while(!idle_xvpic_called && gtk_events_pending())
  gtk_main_iteration();

idle_xvpic_blocked=0;

if(idle_xvpic_called)
  tn_idle_tag=g_idle_add((GSourceFunc)idle_xvpic_load,&idle_xvpic_entry_idle);
}


/* small wrapper function for backend_create_image_from_file() which
 * deals with mrf files and other oddities (currently GIF/PNG).
 *
 * It also copes with loading JPEGs quickly for thumbnails, hence
 * the second arg. :-) The original width/height of the image
 * (which is likely to differ from that of the image returned in the
 * latter case) is returned in orig[wh]p if non-NULL.
 */
xzgv_image *load_image(char *file,int for_thumbnail,
                          int *origwp,int *orighp)
{
xzgv_image *ret;
int origw,origh;

jpeg_exif_orient=0;

ret=backend_create_image_from_file(file);	/* use backend's loader */
if((ret != NULL) && use_exif_orient) jpeg_exif_orient=backend_get_orientation_from_file(file);

origw=0; origh=0;
if(ret)
  {
    origw=ret->w;
    origh=ret->h;
  }

if(origwp) *origwp=origw;
if(orighp) *orighp=origh;

return(ret);
}


GtkAccelGroup *mainwin_accel_group;

GtkWidget *make_menu(
    GtkUIManager *ui_manager,
    char *name,
    char *ui_description,
    GtkActionEntry *entries, int num_entries,
    GtkToggleActionEntry *toggle_entries, int num_toggle_entries,
    GtkRadioActionEntry *radio1_entries, int num_radio1_entries,
    GCallback on_change1,
    GtkRadioActionEntry *radio2_entries, int num_radio2_entries,
    GCallback on_change2)
{
  GtkActionGroup *action_group;
  GtkWidget *menu;
  GError *error = NULL;
  GString *path;

  action_group = gtk_action_group_new(name);
  gtk_action_group_add_actions(action_group, entries, num_entries, NULL);
  if (toggle_entries)
    gtk_action_group_add_toggle_actions(action_group, toggle_entries, num_toggle_entries, NULL);
  if (radio1_entries)
    gtk_action_group_add_radio_actions(action_group, radio1_entries, num_radio1_entries, -1, on_change1, NULL);
  if (radio2_entries)
    gtk_action_group_add_radio_actions(action_group, radio2_entries, num_radio2_entries, -1, on_change2, NULL);

  gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

  gtk_ui_manager_add_ui_from_string(ui_manager, ui_description, -1, &error);
  if (error != NULL) {
    fprintf(stderr, "building menus failed: %s", error->message);
    g_error_free(error);
    exit(1);
  }

  path = g_string_new(NULL);
  g_string_printf(path, "/%s", name);
  menu = gtk_ui_manager_get_widget(ui_manager, path->str);
  g_string_free(path, TRUE);
  g_assert(menu != NULL);

  gtk_menu_set_accel_group(GTK_MENU(menu), mainwin_accel_group);

  return(menu);
}


gint cb_quit(GtkWidget *widget)
{
gtk_main_quit();

/* stop e.g. thumbnail update */
mainwin=NULL;

return(TRUE);
}


void flist_freeze(void)
{
  /* detach model */
  gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), NULL);
}

void flist_thaw(void)
{
  /* re-attach model */
  gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(liststore));
}


int get_path_row_number(GtkTreePath *path)
{
  if (!path)
    return -1;

  if (gtk_tree_path_get_depth(path) != 1)
    return -1;

  gint *indices = gtk_tree_path_get_indices(path);

  if (!indices)
    return -1;

  return indices[0];
}

int get_row_at_pos(int x, int y)
{
  GtkTreePath *path;
  int row;

  gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
      x, y,
      &path,
      NULL,   /* column */
      NULL,   /* cell_x */
      NULL);  /* cell_y */
  row = get_path_row_number(path);
  gtk_tree_path_free(path);

  return row;
}


/* NOTE: The caller takes ownership ot *filename, and is responsible for freeing it. */
void get_row_filename(int row, char **filename)
{
  GtkTreeIter iter;

  if (row < 0 || row >= numrows)
    return;

  gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
  gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MODEL_NAME_COL, filename, -1);
}

void set_row_filename(int row, char *filename)
{
  GtkTreeIter iter;

  if (row < 0 || row >= numrows)
    return;

  gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
  gtk_list_store_set(liststore, &iter, MODEL_NAME_COL, filename, -1);
}


int get_row_thumbnails(int row, GdkPixbuf **pixbuf, GdkPixbuf **small_pixbuf)
{
  GtkTreeIter iter;

  if (row < 0 || row >= numrows)
    return 0;

  gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
  gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MODEL_TN_NORMAL_COL, pixbuf, -1);
  gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MODEL_TN_SMALL_COL, small_pixbuf, -1);

  return (*pixbuf != NULL);
}

void set_row_thumbnails(int row, GdkPixbuf *pixbuf, GdkPixbuf *small_pixbuf)
{
  GtkTreeIter iter;

  if (row < 0 || row >= numrows)
    return;

  gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
  gtk_list_store_set(liststore, &iter, MODEL_TN_NORMAL_COL, pixbuf, -1);
  gtk_list_store_set(liststore, &iter, MODEL_TN_SMALL_COL, small_pixbuf, -1);
}


struct row_data_tag *get_row_data(int row)
{
  GtkTreeIter iter;
  struct row_data_tag *datptr;

  if (row < 0 || row >= numrows)
    return(NULL);

  gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
  gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MODEL_DATA_COL, &datptr, -1);

  return(datptr);
}

void move_to_row(int row, float row_align)
{
  /* these constants are just there to act as named function arguments  */
  const float col_align = 0;
  const gboolean use_align = TRUE;

  GtkTreePath *path;
  GtkTreeViewColumn* column;

  path = gtk_tree_path_new_from_indices(row, -1);
  column = gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 0);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(treeview),
      path, column,
      use_align, row_align, col_align);
  gtk_tree_path_free(path);
}


void enable_sorting(void)
{
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(liststore),
      MODEL_NAME_COL,
      GTK_SORT_ASCENDING);
}

void disable_sorting(void)
{
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(liststore),
      GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
      GTK_SORT_ASCENDING);
}

/* sort model rows -- resort_finish() should usually be called instead */
void sort_model_rows(void)
{
  enable_sorting();
  disable_sorting();
}


void select_row(int row)
{
  GtkTreeSelection *selection;
  GtkTreePath *path;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  path = gtk_tree_path_new_from_indices(row, -1);
  gtk_tree_selection_select_path(selection, path);
  gtk_tree_path_free(path);
}

void unselect_row(int row)
{
  GtkTreeSelection *selection;
  GtkTreePath *path;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  path = gtk_tree_path_new_from_indices(row, -1);
  gtk_tree_selection_unselect_path(selection, path);
  gtk_tree_path_free(path);
}

void unselect_all(void)
{
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  gtk_tree_selection_unselect_all(selection);
}


int first_visible_row(void)
{
  GtkTreePath *start_path, *end_path;
  int row;

  if (!gtk_tree_view_get_visible_range(GTK_TREE_VIEW(treeview), &start_path, &end_path))
    return -1;

  row = get_path_row_number(start_path);

  gtk_tree_path_free(start_path);

  return row;
}

gboolean row_is_visible(int row)
{
  GtkTreePath *start_path, *end_path;
  gboolean ret;

  if (!gtk_tree_view_get_visible_range(GTK_TREE_VIEW(treeview), &start_path, &end_path))
    return(FALSE);

  ret = (get_path_row_number(start_path) <= row) && (row <= get_path_row_number(end_path));

  gtk_tree_path_free(start_path);
  gtk_tree_path_free(end_path);

  return ret;
}

gboolean row_is_fully_visible(int row)
{
  GtkTreePath *path;
  GdkRectangle visible_rect;  /* visible region, in tree coordinates */
  GdkRectangle row_area_bin;  /* area occupied by row, in bin_window coordinates */
  gint tree_x, tree_y;        /* row area in tree coordinates */

  gtk_tree_view_get_visible_rect(GTK_TREE_VIEW(treeview), &visible_rect);

  path = gtk_tree_path_new_from_indices(row, -1);
  gtk_tree_view_get_background_area(GTK_TREE_VIEW(treeview), path, NULL, &row_area_bin);
  gtk_tree_path_free(path);

  /* We'll be operating in tree coordinates, so convert row_area_bin */
  gtk_tree_view_convert_bin_window_to_tree_coords(
      GTK_TREE_VIEW(treeview),
      row_area_bin.x,
      row_area_bin.y,
      &tree_x,
      &tree_y);

  return (tree_y >= visible_rect.y) &&
    ((tree_y + row_area_bin.height) <= (visible_rect.y + visible_rect.height));
}

/* make a row visible if it's partly/fully obscured or `offscreen'. */
void make_visible_if_not(int row)
{
if(!row_is_fully_visible(row))
  move_to_row(row,0.5);
}


/*
 * GtkTreeView does not have the equivalent of GtkCList's focus_row, so we
 * have to manage our own, and draw its cursor rectangle ourselves.
 */

/* get a rectangle (in widget coordinates) for the focus row cursor */
gboolean get_focus_row_rect(GdkRectangle *rect)
{
  GtkTreePath *path;
  GdkRectangle visible_rect;  /* visible region, in tree coordinates */
  GdkRectangle row_area;      /* area occupied by row, in bin_window coordinates */

  /* have the cursor disappear when focus is lost */
  if ((focus_row < 0) || !gtk_widget_has_focus(flist_sw_ebox))
    return(FALSE);

  /* Note that although we are dealing with three different coordinate systems,
   * converting between them is merely a translation, so it does not affect any
   * width/height measurement. */

  /* Horizontal (x) coordinates, anchored to the widget itself */

  /* left anchor is widget's leftmost coordinate (i.e. 0) */
  rect->x = 0;
  /* width is widget's width, also equal to visible width */
  gtk_tree_view_get_visible_rect(GTK_TREE_VIEW(treeview), &visible_rect);
  rect->width = visible_rect.width;

  /* Vertical (y) coordinates, anchored to the focus row */

  /* fetch the area occupied by the focus row */
  path = gtk_tree_path_new_from_indices(focus_row, -1);
  gtk_tree_view_get_cell_area(GTK_TREE_VIEW(treeview), path, NULL, &row_area);
  gtk_tree_path_free(path);
  /* top anchor is focus row's top border */
  /* (This is technically in bin_window coordinates, not widget coordinates,
   * but their y coordinates differ only by the height of the headers, which
   * is 0 in our case, since they are disabled.) */
  rect->y = row_area.y;
  /* height is merely the focus row's height */
  rect->height = row_area.height;

  return(TRUE);
}

/* drawing differs considerably between GTK 2 and 3 */

#if GTK_MAJOR_VERSION >= 3

/* GTK 3: callback for the `draw` signal */
gboolean draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  GdkRectangle rect;
  GtkStyleContext *context;
  GdkRGBA color;

  if (get_focus_row_rect(&rect)) {
    context = gtk_widget_get_style_context(widget);
    gtk_style_context_get_color(context,
        gtk_style_context_get_state(context),
        &color);
    gdk_cairo_set_source_rgba(cr, &color);

    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr,
        /* see https://www.cairographics.org/FAQ/#sharp_lines */
        rect.x + 0.5, rect.y + 0.5,
        /* we need to remove one line width from both dimensions */
        rect.width - 1, rect.height - 1);
    cairo_stroke(cr);
  }

  return GDK_EVENT_PROPAGATE;
}

#else

/*
 * GTK 2: I didn't have much success by simply drawing after the `expose-event`
 * signal (the treeview would overwrite our work, even when using
 * `g_signal_connect_after`), so I resorted to using a timer event that
 * constantly redraws our cursor.  It sucks, but it works.
 *
 * If `user_data` is true, this is a one-shot timer and G_SOURCE_REMOVE will
 * be returned; otherwise, this is a recurrent timer and G_SOURCE_CONTINUE
 * will be returned.  (Remember to use GINT_TO_POINTER() for this.)
 */
gboolean refresh_focus_row_timer_cb(gpointer user_data)
{
  GdkWindow *win;
  GdkRectangle rect;
  GdkGC *gc;

  if (get_focus_row_rect(&rect)) {
    win = gtk_widget_get_window(treeview);

    gc = gdk_gc_new(win);

    gdk_gc_set_subwindow(gc, GDK_INCLUDE_INFERIORS);

    gdk_draw_rectangle(win, gc, FALSE,
        rect.x, rect.y,
        /* we need to remove one line width from both dimensions */
        rect.width - 1, rect.height - 1);

    g_object_unref(gc);
  }

  return (GPOINTER_TO_INT(user_data) ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE);
}

#endif

/* redraw the focus row cursor; this can be used as a standalone function or
 * as a callback */
gboolean refresh_focus_row(void)
{
#if GTK_MAJOR_VERSION >= 3
  /* this will trigger draw_callback() */
  gtk_widget_queue_draw(treeview);

  return GDK_EVENT_PROPAGATE;
#else
  /* add a single-shot timer with a slight delay; g_idle_add() would be too fast */
  g_timeout_add(10, refresh_focus_row_timer_cb, GINT_TO_POINTER(TRUE));

  return FALSE;  /* GDK_EVENT_PROPAGATE */
#endif
}


void set_focus_row(int new_row)
{
focus_row=new_row;
refresh_focus_row();
}


/* gets whether a row is tagged or not.
 * Really just for convenience, and by analogy with set_tagged_state(). :-)
 */
int get_tagged_state(int row)
{
struct row_data_tag *datptr;

if(row<0 || row>=numrows) return(0);

datptr=get_row_data(row);
return(datptr->tagged);
}


/* sets whether a row is tagged or not.
 * tagged=0 to untag, 1 to tag, -1 to toggle.
 */
void set_tagged_state(int row,int tagged)
{
GtkTreeIter iter;
struct row_data_tag *datptr;

datptr=get_row_data(row);
if(datptr->isdir) return;

if(datptr)
  {
  if(tagged==-1)
    datptr->tagged=!datptr->tagged;
  else
    datptr->tagged=tagged;
  }

gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
gtk_list_store_set(liststore, &iter, MODEL_TAGGED_COL, datptr->tagged, -1);
}


/* tag_file and untag_file are used when tagging from the keyboard,
 * or from the tag/untag file menu options.
 */
void cb_tag_file(void)
{
int row=focus_row;

if(row<0) return;

set_tagged_state(row,1);	/* tag */
if(row<numrows-1)		/* move on one */
  {
  set_focus_row(row+1);
  make_visible_if_not(row+1);
  }
}

void cb_untag_file(void)
{
int row=focus_row;

if(row<0) return;

set_tagged_state(row,0);	/* untag */
if(row<numrows-1)		/* move on one */
  {
  set_focus_row(row+1);
  make_visible_if_not(row+1);
  }
}


void cb_tag_all(void)
{
int f;

for(f=0;f<numrows;f++)
  set_tagged_state(f,1);
}


void cb_untag_all(void)
{
int f;

for(f=0;f<numrows;f++)
  set_tagged_state(f,0);
}


void cb_toggle_all(void)
{
int f;

for(f=0;f<numrows;f++)
  set_tagged_state(f,!get_tagged_state(f));
}


void cb_back_to_flist(void)
{
RECURSE_PROTECT_START;

/* unhide selector if it was hidden (whether auto-hidden or not) */
if(hidden)
  {
  gtk_paned_set_position(GTK_PANED(pane),hide_saved_pos);
  hidden=0;
  }

gtk_widget_set_can_focus(flist_sw_ebox, TRUE);
gtk_widget_grab_focus(flist_sw_ebox);

/* XXX kludge: make sure pic is fixed in zoom mode */
pic_win_resized(NULL,NULL);

RECURSE_PROTECT_END;
}


void cb_hide_selector(void)
{
GtkAllocation allocation;
RECURSE_PROTECT_START;

/* this is really a toggle, so show it if it's hidden. */
if(hidden)
  {
  gtk_paned_set_position(GTK_PANED(pane),hide_saved_pos);
  hidden=0;
  }
else
  {
  do_gtk_stuff();   /* in case it's being done immediately after an unhide */
  gtk_widget_get_allocation(sw_for_flist, &allocation);
  hide_saved_pos=allocation.width;
  gtk_paned_set_position(GTK_PANED(pane),1);
  hidden=1;
  }

/* XXX kludge: make sure pic is fixed in zoom mode */
pic_win_resized(NULL,NULL);

RECURSE_PROTECT_END;
}


void cb_iconify(void)
{
GdkWindow *main_gdk_window = gtk_widget_get_window(mainwin);

XIconifyWindow(GDK_WINDOW_XDISPLAY(main_gdk_window),
               GDK_WINDOW_XID(main_gdk_window),
               XScreenNumberOfScreen(XDefaultScreenOfDisplay(
                 GDK_WINDOW_XDISPLAY(main_gdk_window))));
}


gint selector_button_press(GtkWidget *widget,GdkEventButton *event)
{
int row;

if(ignore_selector_input)
  {
  g_signal_stop_emission_by_name(widget, "button_press_event");
  return(TRUE);
  }

/* in theory we should screen out double-clicks, in case someone does
 * that in error. But we seem to get two single-clicks *then* a double-click
 * (seems bizarre to me, surely it should just be single-click then double!?),
 * meaning that the picture *might* be loaded twice, but the selection
 * stays intact. The picture seems to only be loaded twice if the picture
 * has completely loaded before the double-click event is received,
 * so this probably isn't too bad, and actually works out better than
 * screening them out in practice.
 */

switch(event->button)
  {
  case 1:
    if(event->state&GDK_CONTROL_MASK)
      {
      /* stop the treeview widget seeing it */
      g_signal_stop_emission_by_name(widget, "button_press_event");
      return(TRUE);	/* otherwise ignored, we do it on release */
      }
    break;
  
  case 3:
    /* move cursor to row clicked on (if any) */
    row = get_row_at_pos(event->x, event->y);
    cb_back_to_flist();			/* show selector and switch to it */
    if(row>=0 && row<numrows)
      set_focus_row(row);
    
    /* finally we bother showing the menu :-) */
    gtk_menu_popup(GTK_MENU(selector_menu),NULL,NULL,NULL,NULL,3,event->time);
    return(TRUE);
  }

return(FALSE);
}


gint selector_button_release(GtkWidget *widget,GdkEventButton *event)
{
int row;

if(ignore_selector_input)
  {
  g_signal_stop_emission_by_name(widget, "button_release_event");
  return(TRUE);
  }

switch(event->button)
  {
  case 1:
    if(event->state&GDK_CONTROL_MASK)
      {
      row = get_row_at_pos(event->x, event->y);
      if(row>=0 && row<numrows)		/* sanity check :-) */
        set_tagged_state(row,-1);	/* toggle */
      return(TRUE);
      }
    break;
  }

return(FALSE);
}


/* get the pointer's current position on the screen */
void get_pointer_root_coordinates(gint* xp, gint* yp)
{
    GdkDisplay *display = gdk_display_get_default();
    gdk_display_get_pointer(display, NULL, xp, yp, NULL);
}


/* button press on any part of the viewer
 * (except the scrollbars, filtered out kludgily by the next routine)
 */
gint viewer_button_press(GtkWidget *widget,GdkEventButton *event)
{
switch(event->button)
  {
  case 1:	/* left button starts image drag */
    /* but with shift, scales up */
    if(event->state&GDK_SHIFT_MASK)
      {
      cb_scaling_double();
      next_on_release=0;
      break;
      }
    
    /* and with control, scales selected axis only */
    if(event->state&GDK_CONTROL_MASK)
      {
      if(mouse_scale_x)
        cb_xscaling_double();
      else
        cb_yscaling_double();
      next_on_release=0;
      break;
      }
    
    next_on_release=1;
    /* set initial position */
    get_pointer_root_coordinates(&orig_x, &orig_y);
    break;
  
  case 2:	/* middle button is a bit like Esc (handy in auto-hide mode) */
    if(hidden)
      cb_back_to_flist();	/* like Esc - show and focus */
    else
      cb_hide_selector();	/* really toggles it */
    break;
  
  case 3:	/* right button gives menu */
    /* but with shift, scales down */
    if(event->state&GDK_SHIFT_MASK)
      {
      cb_scaling_halve();
      break;
      }
    
    /* and with control, scales down selected axis only */
    if(event->state&GDK_CONTROL_MASK)
      {
      if(mouse_scale_x)
        cb_xscaling_halve();
      else
        cb_yscaling_halve();
      break;
      }
    
    gtk_menu_popup(GTK_MENU(viewer_menu),NULL,NULL,NULL,NULL,3,event->time);
    break;
  }

return(TRUE);
}


gint viewer_button_release(GtkWidget *widget,GdkEventButton *event)
{
switch(event->button)
  {
  case 1:
    if(next_on_release && click_nextpic)
      cb_next_image();
    next_on_release=0;
    break;
  
  default:
    return(FALSE);
  }

return(TRUE);
}


/* button press on one of the image's scrollbars. Needed to override
 * the above, as bringing up the menu by right-clicking on a scrollbar
 * causes all mouse stuff to hang for some reason...!
 */
gint viewer_sb_button_press(GtkWidget *widget,GdkEventButton *event)
{
/* doesn't have to do anything */
return(TRUE);
}


void move_pic(float xadd,float yadd)
{
GtkAdjustment *hadj,*vadj;
float new_x,new_y;

/* add on to adjustment, checking bounds */
hadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));
vadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));

if(xadd)
  {
  new_x=gtk_adjustment_get_value(hadj)+xadd;
  if(new_x<gtk_adjustment_get_lower(hadj)) new_x=gtk_adjustment_get_lower(hadj);
  if(new_x>gtk_adjustment_get_upper(hadj)-gtk_adjustment_get_page_size(hadj)) new_x=gtk_adjustment_get_upper(hadj)-gtk_adjustment_get_page_size(hadj);
  gtk_adjustment_set_value(hadj,new_x);
  }

if(yadd)
  {
  new_y=gtk_adjustment_get_value(vadj)+yadd;
  if(new_y<gtk_adjustment_get_lower(vadj)) new_y=gtk_adjustment_get_lower(vadj);
  if(new_y>gtk_adjustment_get_upper(vadj)-gtk_adjustment_get_page_size(vadj)) new_y=gtk_adjustment_get_upper(vadj)-gtk_adjustment_get_page_size(vadj);
  gtk_adjustment_set_value(vadj,new_y);
  }
}


#if HAVE_DRAG_GESTURES

void on_viewer_drag_update(GtkGestureDrag *gesture,
                           gdouble offset_x, gdouble offset_y,
                           gpointer user_data)
{
gint root_x, root_y;
gint diff_x, diff_y;

next_on_release=0;

/* ignore it if neither scrollbar is onscreen */
if(!gtk_widget_get_visible(gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(sw_for_pic))) &&
   !gtk_widget_get_visible(gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(sw_for_pic))))
  return;

/* XXX! should absorb all pending motion-notify events somehow, and
 * only use the X/Y pos of the last of those!
 */
/* have to use [xy]_root, as the window the events happen on will be moving! */
get_pointer_root_coordinates(&root_x, &root_y);
diff_x = orig_x - root_x;
diff_y = orig_y - root_y;
orig_x = root_x;
orig_y = root_y;

move_pic(diff_x,diff_y);
}

#else

/* scroll dragging is not available; make sure drags don't register as clicks */
gint viewer_motion(GtkWidget *widget,GdkEventMotion *event)
{
  next_on_release=0;
  return(TRUE);
}

#endif


/* used by gtk_menu_popup() calls invoked from keyboard */
void keyboard_menu_pos(GtkMenu *menu,gint *xp,gint *yp,gboolean *push_in,GtkWidget *data)
{
GtkAllocation allocation;

gtk_widget_get_allocation(data, &allocation);
gdk_window_get_position(gtk_widget_get_window(mainwin),xp,yp);
*xp+=allocation.x;
*yp+=allocation.y;
}


/* this may call pic_win_resized, and can inherit the recursion problem
 * of render_pixbuf, so be careful.
 */
int common_key_press(GdkEventKey *event)
{
GtkAllocation allocation;

gtk_widget_get_allocation(sw_for_flist, &allocation);
int maxpos,oldpos,pos=allocation.width;
int step=20;

if(event->state&GDK_CONTROL_MASK)
  step=5;

switch(event->keyval)
  {
  case GDK_KEY_bracketleft:		/* [ */
    oldpos=pos;
    pos-=step;
    if(pos<1) pos=1;
    if(pos!=oldpos)
      {
      gtk_paned_set_position(GTK_PANED(pane),pos);
      pic_win_resized(NULL,NULL);	/* XXX kludge for zoom mode */
      }
    return(TRUE);
  
  case GDK_KEY_bracketright:	/* ] */
    gtk_widget_get_allocation(mainwin, &allocation);
    maxpos=allocation.width;
    oldpos=pos;
    pos+=step;
    if(pos>maxpos) pos=maxpos;
    if(pos!=oldpos)
      {
      gtk_paned_set_position(GTK_PANED(pane),pos);
      pic_win_resized(NULL,NULL);	/* XXX kludge for zoom mode */
      }
    return(TRUE);
  
  case GDK_KEY_asciitilde:		/* ~ */
    if(pos!=default_sel_width)
      {
      hidden=0;				/* also treat as unhide */
      gtk_paned_set_position(GTK_PANED(pane),default_sel_width);
      pic_win_resized(NULL,NULL);	/* XXX kludge for zoom mode */
      }
    return(TRUE);
  
  default:
    return(FALSE);
  }
}




void cb_viewer_next_tagged(void)
{
cb_nextprev_tagged_image(1,1);
}

void cb_viewer_prev_tagged(void)
{
cb_nextprev_tagged_image(0,1);
}


gint viewer_key_press(GtkWidget *widget,GdkEventKey *event)
{
/* this first bit is an adapted RECURSE_PROTECT_START */
static int here=0;

if(here)
  {
  /* stop the event to avoid weirdness */
  g_signal_stop_emission_by_name(widget, "key_press_event");
  return(TRUE);
  }

here=1;

/* This should only handle the minimum necessary, with most things
 * being done via accelerators for menu items.
 *
 * The main things to handle here are the cursors, page up/down, etc.
 */

/* XXX these are zgv-ish for now; want that as the default, but
 * really should have an optional mouse-reflecting mode using
 * the adjustments' step size and page increment.
 */

/* treat shift-cursor as page up/down/left/right */
if((event->state&GDK_SHIFT_MASK))
  switch(event->keyval)
    {
    case GDK_KEY_Left:	goto page_left;
    case GDK_KEY_Right:	goto page_right;
    case GDK_KEY_Up:	goto page_up;
    case GDK_KEY_Down:	goto page_down;
    }

switch(event->keyval)
  {
  case GDK_KEY_space:
    if((event->state&GDK_CONTROL_MASK))
      cb_tag_then_next();
    else
      cb_next_image();
    break;
  
  case GDK_KEY_slash:
    cb_viewer_next_tagged();
    break;
  
  case GDK_KEY_question:
    cb_viewer_prev_tagged();
    break;
  
  /* the way this works also means that e.g. control-shift-h will
   * move a small amount (like unmodified h), but that doesn't hurt,
   * and I s'pose at least it's consistent. :-)
   */
  case GDK_KEY_Left: case GDK_KEY_H:
    move_pic((event->state&GDK_CONTROL_MASK)?-10.:-100., 0.);
    break;
  case GDK_KEY_Right: case GDK_KEY_L:
    move_pic((event->state&GDK_CONTROL_MASK)?+10.:+100., 0.);
    break;
  case GDK_KEY_Up: case GDK_KEY_K:
    move_pic(0., (event->state&GDK_CONTROL_MASK)?-10.:-100.);
    break;
  case GDK_KEY_Down: case GDK_KEY_J:
    move_pic(0., (event->state&GDK_CONTROL_MASK)?+10.:+100.);
    break;

  case GDK_KEY_h:
    move_pic(-10.,0.);
    break;
  case GDK_KEY_l:
    move_pic(+10.,0.);
    break;
  case GDK_KEY_k:
    move_pic(0.,-10.);
    break;
  case GDK_KEY_j:
    move_pic(0.,+10.);
    break;
  
  case GDK_KEY_Page_Up: case GDK_KEY_u:
  page_up:
    if(event->keyval!=GDK_KEY_u || (event->state&GDK_CONTROL_MASK))
      move_pic(0.,-0.9*gtk_adjustment_get_page_size(GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(sw_for_pic)))));
    else
      {
      RECURSE_PROTECT_END;
      return(FALSE);	/* don't stop event if not handled */
      }
    break;
  case GDK_KEY_Page_Down: case GDK_KEY_v:
  page_down:
    if(event->keyval!=GDK_KEY_v || (event->state&GDK_CONTROL_MASK))
      move_pic(0.,+0.9*gtk_adjustment_get_page_size(GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(sw_for_pic)))));
    else
      {
      RECURSE_PROTECT_END;
      return(FALSE);
      }
    break;
  case GDK_KEY_minus:
  page_left:
    move_pic(-0.9*gtk_adjustment_get_page_size(GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
      GTK_SCROLLED_WINDOW(sw_for_pic)))), 0.);
    break;
  case GDK_KEY_equal:
  page_right:
    move_pic(+0.9*gtk_adjustment_get_page_size(GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
      GTK_SCROLLED_WINDOW(sw_for_pic)))), 0.);
    break;
  
  case GDK_KEY_Home: case GDK_KEY_a:
    if(event->keyval!=GDK_KEY_a || (event->state&GDK_CONTROL_MASK))
      move_pic(-32768.,-32768.);  /* X window size limit is 32767x32767 */
    else
      {
      RECURSE_PROTECT_END;	/* don't stop event if not handled */
      return(FALSE);
      }
    break;
  case GDK_KEY_End: case GDK_KEY_e:
    if(event->keyval!=GDK_KEY_e || (event->state&GDK_CONTROL_MASK))
      move_pic(+32768.,+32768.);
    else
      {
      RECURSE_PROTECT_END;
      return(FALSE);
      }
    break;
  
  case GDK_KEY_Tab:		/* also treat tab as esc */
    cb_back_to_flist();
    break;
  
  case GDK_KEY_F10: case GDK_KEY_Menu:
    /* pop-up menu on F10 (Emacs-like) or Menu */
    gtk_menu_popup(GTK_MENU(viewer_menu),NULL,NULL,
                   (GtkMenuPositionFunc)keyboard_menu_pos,sw_for_pic,
                   3,event->time);
    break;
  
  default:
    /* check for non-menu-item keys common to selector and viewer */
    if(!common_key_press(event))
      {
      RECURSE_PROTECT_END;
      return(FALSE);	/* don't stop event if not handled */
      }
  }

/* if we handled it, stop anything else getting the event.
 * This is needed to handle us tabbing into the viewer and using it;
 * in that case, the selector still accepts focus, so (e.g.) pressing
 * down wouldn't work properly.
 */
g_signal_stop_emission_by_name(widget, "key_press_event");

RECURSE_PROTECT_END;
return(TRUE);
}


void view_focus_row_file(void)
{
int row;

/* skip it early if we're already busy */
if(in_nextprev) return;

in_nextprev=1;	/* in effect :-) */

/* one difference from normal treeview keyboard-select behaviour;
 * we always select (rather than toggling), even if image was
 * previously selected.
 */
row=focus_row;
if(row>=0 && row<numrows)
  {
  unselect_all();
  /* this sets current_selection and zeroes in_nextprev too */
  select_row(row);
  in_nextprev=0;
  }
else
  in_nextprev=0;
}


void cb_nextprev_tagged_image(int next,int view)
{
int f,dest,row=focus_row;
struct row_data_tag *datptr;
int incr=(next?1:-1);

if(in_nextprev) return;

dest=-1;
for(f=row+incr;(next && f<numrows) || (!next && f>=0);f+=incr)
  {
  datptr=get_row_data(f);
  if(datptr && datptr->tagged)
    {
    dest=f;
    break;
    }
  }

if(dest==-1)
  return;

set_focus_row(dest);
make_visible_if_not(dest);
if(view)
  view_focus_row_file();
}


void cb_selector_next_tagged(void)
{
cb_nextprev_tagged_image(1,0);
}

void cb_selector_prev_tagged(void)
{
cb_nextprev_tagged_image(0,0);
}


gint selector_key_press(GtkWidget *widget,GdkEventKey *event)
{
static int goto_next_char=0,goto_next_evtime;
/* this first bit is an adapted RECURSE_PROTECT_START */
static int here=0;

if(here || ignore_selector_input)
  {
  /* stop the event to avoid weirdness */
  g_signal_stop_emission_by_name(widget, "key_press_event");
  return(TRUE);
  }

here=1;

/* This is for the odd selector thing which isn't on the menus. */

if(goto_next_char)
  {
  /* completely ignore any shift keypress! */
  if(event->keyval==GDK_KEY_Shift_L || event->keyval==GDK_KEY_Shift_R)
    {
    RECURSE_PROTECT_END;
    return(FALSE);
    }
  
  goto_next_char=0;
  
  if(event->time-goto_next_evtime<=2000 &&	/* ignore if >2 secs later */
     event->keyval>=33 && event->keyval<=126)
    {
    int f,nofiles=1,found=0;
    struct row_data_tag *datptr;
    char *ptr;
    
    /* go to first file (not dir) which starts with that char.
     * if there isn't one, go to first which starts with a later
     * char.
     * also, if there aren't any files (just dirs) don't move;
     * otherwise, if there are no files with 1st char >=keyval,
     * go to last file.
     *
     * nicest way to do this would be a binary search, but that
     * would complicate matters; a linear search may be crude
     * but it's still blindingly fast. And at least a linear one
     * gives *predictably* useless results when not using
     * sort-by-name. :-)
     */
    for(f=0;f<numrows;f++)
      {
      datptr=get_row_data(f);
      if(!datptr->isdir)
        {
        char first_char;

        get_row_filename(f,&ptr);
        first_char = ptr[0];
        g_free(ptr);

        nofiles=0;
        if(first_char>=event->keyval)
          {
          set_focus_row(f);
          found=1;
          break;
          }
        }
      }
    
    /* if didn't find one >=keyval but there *are* files in the
     * dir, go to the last one.
     */
    if(!found && !nofiles)
      set_focus_row(numrows-1);
    
    /* recentre on it */
    if(!nofiles)
      move_to_row(focus_row,0.5);
    }
  }
else
  {
  int oldrow,incdec,up;
  int row=focus_row;
  float vpage;
  GtkAdjustment *adj;
  float adj_value;
  
  /* if not a goto-next-char char... */
  switch(event->keyval)
    {
    case GDK_KEY_Return:	/* select pic */
    case GDK_KEY_space:	/* handle this too, for consistency */
      view_focus_row_file();
      break;

    case GDK_KEY_slash:
      cb_selector_next_tagged();
      break;
      
    case GDK_KEY_question:
      cb_selector_prev_tagged();
      break;
      
    case GDK_KEY_apostrophe:
    case GDK_KEY_g:
      goto_next_char=1;
      goto_next_evtime=event->time;
      break;

    case GDK_KEY_k:		/* up */
    case GDK_KEY_Up:
      if(row>0)
        {
        set_focus_row(row=row-1);
        if(!row_is_fully_visible(row))
          move_to_row(row,0.);
        }
      break;
    case GDK_KEY_j:		/* down */
    case GDK_KEY_Down:
      if(row<numrows-1)
        {
        set_focus_row(row=row+1);
        if(!row_is_fully_visible(row))
          move_to_row(row,1.);
        }
      break;

#define RET_IF_NOT_CONTROL	\
      if(!(event->state&GDK_CONTROL_MASK)) {RECURSE_PROTECT_END;return(FALSE);}
      
    case GDK_KEY_u: case GDK_KEY_v:	/* ctrl-u/v, like page up/down */
      RET_IF_NOT_CONTROL;
    case GDK_KEY_Page_Up: case GDK_KEY_Page_Down:
      up=((event->keyval==GDK_KEY_u) || (event->keyval==GDK_KEY_Page_Up));
      oldrow=row;
      vpage=gtk_adjustment_get_page_size(GTK_ADJUSTMENT(
        gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(treeview))));
      incdec=(int)((vpage/
                    (1+(thin_rows?ROW_HEIGHT_THIN:ROW_HEIGHT_NORMAL)))+0.5);
      /* next statement is bug-compatible with true page up/down :-)
       * (i.e. page up/down have no effect in a window where the list
       * is less than 1.5 rows high)
       */
      if(incdec>0) incdec--;
      row+=(up?-incdec:incdec);
      if(row<0) row=0;
      if(row>=numrows) row=numrows-1;
      if(row!=oldrow)
        {
        set_focus_row(row);
        if(!row_is_fully_visible(row))
          move_to_row(row,up?0.:1.);
        }
      break;
      
    case GDK_KEY_Home:
    case GDK_KEY_a:		/* ctrl-a, like ctrl-home */
      RET_IF_NOT_CONTROL;
      if(numrows)
        set_focus_row(0),make_visible_if_not(0);
      break;
    case GDK_KEY_End:
    case GDK_KEY_e:		/* ctrl-e, like ctrl-end */
      RET_IF_NOT_CONTROL;
      if(numrows)
        set_focus_row(numrows-1),make_visible_if_not(numrows-1);
      break;
  
    case GDK_KEY_semicolon:		/* do the same as colon */
    case GDK_KEY_colon:		/* XXX actually, menu binding seems broken? */
      cb_file_details();
      break;

    case GDK_KEY_KP_Add:
    case GDK_KEY_plus:	/* may be preferable on some non-US/UK keyboards, and on laptops */
    case GDK_KEY_0:		/* last-ditch alternative for non-US/UK laptops */
      if(event->state&GDK_MOD1_MASK)
        cb_tag_all();
      else
        cb_tag_file();
      break;
    
    case GDK_KEY_KP_Subtract:
    case GDK_KEY_9:
      if(event->state&GDK_MOD1_MASK)
        cb_untag_all();
      else
        cb_untag_file();
      break;
    
    case GDK_KEY_F10: case GDK_KEY_Menu:
      /* pop-up menu, as for viewer */
      gtk_menu_popup(GTK_MENU(selector_menu),NULL,NULL,
                     (GtkMenuPositionFunc)keyboard_menu_pos,sw_for_flist,
                     3,event->time);
      break;
    
    case GDK_KEY_Left:
    case GDK_KEY_Right:
      adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw_for_flist));
      adj_value = gtk_adjustment_get_value(adj);
      adj_value +=
          (event->keyval == GDK_KEY_Right ? 1 : -1) *
          (event->state & GDK_CONTROL_MASK
            ? gtk_adjustment_get_page_increment(adj)
            : gtk_adjustment_get_step_increment(adj));
      /* restrict the value to the scrollbar's range */
      adj_value = MAX(adj_value, gtk_adjustment_get_lower(adj));
      adj_value = MIN(adj_value, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
      gtk_adjustment_set_value(adj, adj_value);
      break;

    default:
      /* check for non-menu-item keys common to selector and viewer */
      if(!common_key_press(event))
        {
        RECURSE_PROTECT_END;
        return(FALSE);	/* don't stop event if not handled */
        }
    }
  }

/* if we handled it, stop anything else getting the event. */
g_signal_stop_emission_by_name(widget, "key_press_event");

RECURSE_PROTECT_END;
return(TRUE);
}



void get_zoomed_size(int *swp,int *shp)
{
GtkAllocation allocation;

gtk_widget_get_allocation(sw_for_pic, &allocation);
int scrnwide=allocation.width-sw_border_width;
int scrnhigh=allocation.height-sw_border_height;
int width=theimage->w;
int height=theimage->h;

if (!zoom_panorama)
  {
    /* try landscapey */
    *swp=scrnwide; *shp=(scrnwide*height)/width;
    if(*shp>scrnhigh)
      /* no, oh well portraity then */
      *shp=scrnhigh,*swp=(scrnhigh*width)/height;
  }
else
  {
    if (((float)width/scrnwide)>((float)height/scrnhigh))
      /* pan horizontally */
      zoom_panorama_sb=0,*swp=(scrnhigh*width)/height,*shp=scrnhigh;
    else
      /* pan vertically */
      zoom_panorama_sb=1,*swp=scrnwide,*shp=(scrnwide*height)/width;
  }
  /* don't expand if it's shrink-only. */
  if(zoom_reduce_only && (*swp>width || *shp>height))
    *swp=width,*shp=height;
}


/* render pixbuf from image, resize drawing area to fit, and just
 * generally update things. Call this to update the image after pretty
 * much any change at all. :-)
 *
 * NB: this calls do_gtk_stuff(), so callers sensitive to recursion
 * beware! (In other words, defend against recursion with something like
 * the in_render stuff below, or RECURSE_PROTECT_START/END.)
 */
void render_pixbuf(int reset_pos)
{
int sw,sh;
int width,height;
int scaling_up_enabled=0;
static int in_render=0;

/* never bother if no image is loaded */
if(!theimage) return;

if(in_render) return;
in_render=1;

width=theimage->w;
height=theimage->h;

sw=width; sh=height;

if(zoom)
  get_zoomed_size(&sw,&sh);

if(!zoom)
  {
    if(xscaling!=1 || yscaling!=1)	/* other non-1:1 scales */
      {
      if(xscaling<-1) sw/=-xscaling; else sw*=xscaling;
      if(yscaling<-1) sh/=-yscaling; else sh*=yscaling;
      if(sw<1) sw=1;
      if(sh<1) sh=1;
      if(sw>32767) sw=32767;
      if(sh>32767) sh=32767;
      /* XXX could do with a combined test, to limit the resulting
       * image to at most N megs.
       */
      }
  }

/* so now our image will be sw x sh */
if(!scaling_up_enabled)
  backend_render_pixbuf_for_image(theimage,sw,sh);

if(thepixbuf)
  backend_pixbuf_destroy(thepixbuf),thepixbuf=NULL;
if(!scaling_up_enabled)
  thepixbuf=backend_get_and_detach_pixbuf(theimage);

/* set drawing area to size of pixbuf (also generates expose event) */
gtk_widget_set_size_request(align,sw,sh);
gtk_widget_set_size_request(image_widget,sw,sh);

/* go back to top-left */
if(reset_pos)
  {
  gtk_adjustment_set_value(
    gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw_for_pic)),0.);
  gtk_adjustment_set_value(
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw_for_pic)),0.);
  }

/* let scrollbars appear/disappear as needed *before* we put pixbuf on.
 * Without this you get a nasty `bounce' effect whenever scrollbars
 * appear/disappear. But it can still happen when resizing. (XXX)
 */
do_gtk_stuff();

/* put pixbuf onto window as background. We could then free it, but
 * we don't - see idle_zoom_resize() for why. (Basically, we restore
 * the pic there after (to avoid `bounce') having removed it.)
 */
if(thepixbuf)
  {
  gtk_image_set_from_pixbuf(GTK_IMAGE(image_widget), thepixbuf);
  }

in_render=0;
}


/* see below for what this is */
void idle_zoom_resize(void)
{
/* make sure we won't be called again */
g_source_remove(zoom_resize_idle_tag);
zoom_resize_idle_tag=-1;

if(zoom)
  render_pixbuf(1);	/* different size, render again */
else
  {
  gtk_image_set_from_pixbuf(GTK_IMAGE(image_widget), thepixbuf);
  }
}


/* note that in zoom mode this inherits the recursion problem
 * of render_pixbuf due to idle_zoom_resize, so be careful.
 */
gint pic_win_resized(GtkWidget *widget,GdkEventConfigure *event)
{
/* NB: this shouldn't use widget or event, as it's sometimes
 * called with them both being NULL (by the auto-hide stuff).
 */

if(zoom_resize_idle_tag!=-1)
  g_source_remove(zoom_resize_idle_tag);

/* using resize priority gives better results if using `opaque resize',
 * but seems to break `normal' resizing. Not a good tradeoff. :-(
 */
zoom_resize_idle_tag=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE /*GTK_PRIORITY_RESIZE*/,
                                           (GSourceFunc)idle_zoom_resize,NULL,NULL);
return(FALSE);
}


void set_row_height(int height)
{
  GList *column_list;

  column_list = gtk_tree_view_get_columns(GTK_TREE_VIEW(treeview));
  for (GList *l = column_list; l != NULL; l = l->next)
  {
    GtkTreeViewColumn *column = l->data;
    GList *renderer_list;

    renderer_list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
    for (GList *ll = renderer_list; ll != NULL; ll = ll->next)
    {
      GtkCellRenderer *renderer = ll->data;
      g_object_set(renderer, "height", height, NULL);
    }
    g_list_free(renderer_list);
  }
  g_list_free(column_list);
}

void fix_row_heights(void)
{
set_row_height(thin_rows?ROW_HEIGHT_THIN:ROW_HEIGHT_NORMAL);
}


void set_thumbnail_column_width(void)
{
  GtkTreeViewColumn *column;

  column = gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), VIEW_TN_COL);
  gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width(column,
                           thin_rows ? (80 / ROW_HEIGHT_DIV + 1) : 80);
}


void same_centre(int *xp,int *yp,int newxsc,int newysc,
                 int oldx,int oldy,int oldxsc,int oldysc)
{
int xa,ya,sw,sh;
int width,height;
int scrnwide,scrnhigh;

if(!theimage) return;

width=theimage->w;
height=theimage->h;
scrnwide=gtk_adjustment_get_page_size(GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic))));
scrnhigh=gtk_adjustment_get_page_size(GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic))));

xa=ya=0;
sw=oldxsc*width;
sh=oldysc*height;
if(sw<scrnwide) xa=(scrnwide-sw)>>1;
if(sh<scrnhigh) ya=(scrnhigh-sh)>>1;  

/* finds centre of old visible area, and makes it centre of new one */
*xp=(oldx-xa+(scrnwide>>1))*newxsc/oldxsc;
*yp=(oldy-ya+(scrnhigh>>1))*newysc/oldysc;

xa=ya=0;
sw=newxsc*width;
sh=newysc*height;
if(sw<scrnwide) xa=(scrnwide-sw)>>1;
if(sh<scrnhigh) ya=(scrnhigh-sh)>>1;  

*xp-=(scrnwide>>1)+xa;
*yp-=(scrnhigh>>1)+ya;
}


/* call this before render_pixbuf() */
void get_new_centre(int oldxsc,int oldysc,int newxsc,int newysc,
                    float *xp,float *yp)
{
GtkAdjustment *hadj,*vadj;
int oldx,oldy,x,y;

hadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));
vadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));

oldx=(int)gtk_adjustment_get_value(hadj);
oldy=(int)gtk_adjustment_get_value(vadj);
same_centre(&x,&y,newxsc,newysc,oldx,oldy,oldxsc,oldysc);

*xp=(float)x; *yp=(float)y;
}


/* call this after render_pixbuf() */
void move_to_new_centre(float new_x,float new_y)
{
GtkAdjustment *hadj,*vadj;

hadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));
vadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));

if(new_x<gtk_adjustment_get_lower(hadj)) new_x=gtk_adjustment_get_lower(hadj);
if(new_x>gtk_adjustment_get_upper(hadj)-gtk_adjustment_get_page_size(hadj)) new_x=gtk_adjustment_get_upper(hadj)-gtk_adjustment_get_page_size(hadj);
gtk_adjustment_set_value(hadj,new_x);

if(new_y<gtk_adjustment_get_lower(vadj)) new_y=gtk_adjustment_get_lower(vadj);
if(new_y>gtk_adjustment_get_upper(vadj)-gtk_adjustment_get_page_size(vadj)) new_y=gtk_adjustment_get_upper(vadj)-gtk_adjustment_get_page_size(vadj);
gtk_adjustment_set_value(vadj,new_y);
}


/* this inherits the same recursion problem as render_pixbuf(),
 * so be careful.
 */
void scaling_finish(int oldxsc,int oldysc)
{
float x,y;

/* fairly hairy... :-/ */
if(oldxsc!=xscaling || oldysc!=yscaling)
  get_new_centre(oldxsc,oldysc,xscaling,yscaling,&x,&y);
render_pixbuf(0);
gtk_widget_hide(image_widget);
if(oldxsc!=xscaling || oldysc!=yscaling)
  move_to_new_centre(x,y);
gtk_widget_show(image_widget);
gdk_flush();
}


/* turn off zoom (if enabled) without a call to render_pixbuf() */
void undo_zoom(void)
{
if(!zoom) return;

listen_to_toggles=0;
zoom=0;
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_pic),
                               GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
xscaling=yscaling=1;
gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(zoom_widget),zoom);
listen_to_toggles=1;
}



/* the size test at the end means the scale-both/xscale/yscale
 * routines need to be atomic, so the least painful approach seems
 * to be to have a generic do-x-and/or-y routine for each scaling
 * option, then separate both/x/y routines calling that.
 */

void xy_scaling_double(int do_x,int do_y)
{
static int in_routine=0;
int xtmp=xscaling,ytmp=yscaling,oldxsc=xscaling,oldysc=yscaling;

/* if there's no image, don't do anything */
if (!theimage) return;

/* if recursed, don't bother */
if(in_routine) return;
in_routine=1;

#define SCALE(ntmp,nscaling) \
  do {						\
  ntmp=(nscaling<-1?nscaling/2:nscaling*2);	\
  if(ntmp==-1 || ntmp==0) ntmp=1;		\
  if(ntmp>512) ntmp=512;			\
  } while(0)

if(do_x) SCALE(xtmp,xscaling);
if(do_y) SCALE(ytmp,yscaling);

#undef SCALE

if(theimage->w*xtmp<=32767 && theimage->h*ytmp<=32767)
  {
  undo_zoom();
  xscaling=xtmp;
  yscaling=ytmp;
  scaling_finish(oldxsc,oldysc);
  }

in_routine=0;
}

void cb_scaling_double(void)
{
xy_scaling_double(1,1);
}

void cb_xscaling_double(void)
{
xy_scaling_double(1,0);
}

void cb_yscaling_double(void)
{
xy_scaling_double(0,1);
}


void xy_scaling_add(int do_x,int do_y)
{
static int in_routine=0;
int xtmp=xscaling,ytmp=yscaling,oldxsc=xscaling,oldysc=yscaling;

/* if there's no image, don't do anything */
if (!theimage) return;

/* if recursed, don't bother */
if(in_routine) return;
in_routine=1;

#define SCALE(ntmp,nscaling) \
  do {						\
  ntmp=nscaling+1;				\
  if(ntmp==-1 || ntmp==0) ntmp=1;		\
  if(ntmp>512) ntmp=512;			\
  } while(0)

if(do_x) SCALE(xtmp,xscaling);
if(do_y) SCALE(ytmp,yscaling);

#undef SCALE

if(theimage->w*xtmp<=32767 && theimage->h*ytmp<=32767)
  {
  undo_zoom();
  xscaling=xtmp;
  yscaling=ytmp;
  scaling_finish(oldxsc,oldysc);
  }

in_routine=0;
}

void cb_scaling_add(void)
{
xy_scaling_add(1,1);
}

void cb_xscaling_add(void)
{
xy_scaling_add(1,0);
}

void cb_yscaling_add(void)
{
xy_scaling_add(0,1);
}


void xy_scaling_halve(int do_x,int do_y)
{
static int in_routine=0;
int xtmp=xscaling,ytmp=yscaling,oldxsc=xscaling,oldysc=yscaling;

/* if there's no image, don't do anything */
if (!theimage) return;

/* if recursed, don't bother */
if(in_routine) return;
in_routine=1;

#define SCALE(ntmp,nscaling) \
  do {					\
  ntmp=nscaling;			\
  if(ntmp==1) ntmp=-1;			\
  if(ntmp>1) ntmp/=2; else ntmp*=2;	\
  } while(0)

if(do_x) SCALE(xtmp,xscaling);
if(do_y) SCALE(ytmp,yscaling);

#undef SCALE

if(xtmp<SCALING_DOWN_LIMIT || ytmp<SCALING_DOWN_LIMIT)
  {
  in_routine=0;
  return;
  }

undo_zoom();
xscaling=xtmp;
yscaling=ytmp;
scaling_finish(oldxsc,oldysc);

in_routine=0;
}

void cb_scaling_halve(void)
{
xy_scaling_halve(1,1);
}

void cb_xscaling_halve(void)
{
xy_scaling_halve(1,0);
}

void cb_yscaling_halve(void)
{
xy_scaling_halve(0,1);
}


void xy_scaling_sub(int do_x,int do_y)
{
static int in_routine=0;
int xtmp=xscaling,ytmp=yscaling,oldxsc=xscaling,oldysc=yscaling;

/* if there's no image, don't do anything */
if (!theimage) return;

/* if recursed, don't bother */
if(in_routine) return;
in_routine=1;

#define SCALE(ntmp,nscaling) \
  do {			\
  ntmp=nscaling;	\
  if(ntmp==1) ntmp=-1;	\
  ntmp--;		\
  } while(0)

if(do_x) SCALE(xtmp,xscaling);
if(do_y) SCALE(ytmp,yscaling);

#undef SCALE

if(xtmp<SCALING_DOWN_LIMIT || ytmp<SCALING_DOWN_LIMIT)
  {
  in_routine=0;
  return;
  }

undo_zoom();
xscaling=xtmp;
yscaling=ytmp;
scaling_finish(oldxsc,oldysc);

in_routine=0;
}

void cb_scaling_sub(void)
{
xy_scaling_sub(1,1);
}

void cb_xscaling_sub(void)
{
xy_scaling_sub(1,0);
}

void cb_yscaling_sub(void)
{
xy_scaling_sub(0,1);
}


void cb_normal(void)
{
static int here=0;
if(here) return;
here=1;

undo_zoom();
xscaling=yscaling=1;
render_pixbuf(1);

here=0;
}


/* Any callbacks which call render_pixbuf() `must' avoid recursion,
 * but this is most obvious with toggles like zoom, so toggles really
 * *must* be especially careful about this.
 *
 * (The obvious approach for toggles is to (re)use listen_to_toggles.)
 */
void toggle_zoom(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

zoom=!zoom;
if(zoom)
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_pic),
                               (zoom_panorama&&zoom_panorama_sb)?GTK_POLICY_NEVER:GTK_POLICY_AUTOMATIC,
                               (zoom_panorama&&!zoom_panorama_sb)?GTK_POLICY_NEVER:GTK_POLICY_AUTOMATIC);
else
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_pic),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
xscaling=yscaling=1;
render_pixbuf(1);

listen_to_toggles=1;
}


void toggle_zoom_reduce(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

zoom_reduce_only=!zoom_reduce_only;
render_pixbuf(1);

listen_to_toggles=1;
}


void toggle_zoom_panorama(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

zoom_panorama=!zoom_panorama;
render_pixbuf(1);

listen_to_toggles=1;
}


void toggle_interp(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

interp=!interp;

backend_set_interp(interp);

render_pixbuf(0);

listen_to_toggles=1;
}


void toggle_mouse_x(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles) return;

mouse_scale_x=!mouse_scale_x;
}


void toggle_revert(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;

revert=!revert;
}


void toggle_revert_orient(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;

revert_orient=!revert_orient;
}


void toggle_exif_orient(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;

use_exif_orient=!use_exif_orient;
}


void toggle_thin_rows(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
GtkTreeViewColumn *column;

if(!listen_to_toggles || in_nextprev) return;

listen_to_toggles=0;

flist_freeze();

thin_rows=!thin_rows;
fix_row_heights();
set_thumbnail_column_width();

/* switch model source column for the view thumbnail column */
column = gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), VIEW_TN_COL);
/* GTK does not support modifying an attribute, so clear them all and re-add */
gtk_cell_layout_clear_attributes(GTK_CELL_LAYOUT(column), thumbnail_renderer);
gtk_tree_view_column_add_attribute(column, thumbnail_renderer,
    "pixbuf", (thin_rows ? MODEL_TN_SMALL_COL : MODEL_TN_NORMAL_COL));

flist_thaw();

/* this is required to avoid minor redraw-related position gliches
 * when moving the focus row (below).
 */
do_gtk_stuff();

/* the selection is quite likely to have been `lost', so
 * always move, even if already visible (for consistency). What we
 * do is force focus row to selected one (if one is selected), then
 * move visible window to focus row.
 */
if(current_selection!=-1)
  set_focus_row(current_selection);
move_to_row(focus_row,0.5);

listen_to_toggles=1;
}


void toggle_status(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;

have_statusbar=!have_statusbar;
if(have_statusbar)
  gtk_widget_show(statusbar);
else
  gtk_widget_hide(statusbar);
}


void toggle_tn_msgs(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles) return;

tn_msgs=!tn_msgs;
}


void toggle_auto_hide(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

auto_hide=!auto_hide;
if(!auto_hide && hidden)
  {
  /* restore selector to most-recently-saved size (or default) */
  gtk_paned_set_position(GTK_PANED(pane),hide_saved_pos);
  hidden=0;
  pic_win_resized(NULL,NULL);	/* XXX kludge for zoom mode */
  }

listen_to_toggles=1;
}


void cb_show_images(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

show_images_only = !show_images_only;
reinit_dir(0,1);

listen_to_toggles=1;
}


void cb_flip(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
backend_flip_vert(theimage);
orient_current_state=orient_state_flip[orient_current_state];
render_pixbuf(1);
RECURSE_PROTECT_END;
}


void cb_mirror(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
backend_flip_horiz(theimage);
orient_current_state=orient_state_mirror[orient_current_state];
render_pixbuf(1);
RECURSE_PROTECT_END;
}


void cb_rot_cw(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
/* swap x and y scaling, since the effect if we don't do that
 * is of the image mysteriously changing. :-)
 */
backend_rotate_cw(theimage);
orient_current_state=orient_state_rot_cw[orient_current_state];
swap_xyscaling();
render_pixbuf(1);
RECURSE_PROTECT_END;
}


void cb_rot_acw(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
backend_rotate_acw(theimage);
orient_current_state=orient_state_rot_acw[orient_current_state];
swap_xyscaling();
render_pixbuf(1);
RECURSE_PROTECT_END;
}


void cb_normal_orient(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
if(orient_current_state!=0)
  {
    orient_change_state(orient_current_state,0);
    orient_current_state=0;
    render_pixbuf(1);
  }
RECURSE_PROTECT_END;
}


int thumbnail_read_running(void)
{
return(tn_idle_tag!=-1);
}


void start_thumbnail_read(void)
{
if(thumbnail_read_running()) return;	/* don't if it's already running */

if(!numrows) return;		/* this is surely impossible, but WTF :-) */

/* we pass pointer to zeroed `entry', saving the difficulty
 * of dealing with when to zero this in the idle function itself.
 * Ditto with last adjustment, though this is a bit ugly. :-)
 */
idle_xvpic_lastadjval=gtk_adjustment_get_value(gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(treeview)));
idle_xvpic_jumped=0;
idle_xvpic_entry_idle=0;
tn_idle_tag=g_idle_add((GSourceFunc)idle_xvpic_load,&idle_xvpic_entry_idle);

/* the "" is a crappy way to disable it, but it's hairy otherwise */
gtk_statusbar_push(GTK_STATUSBAR(statusbar),tn_id,
                   tn_msgs?"Reading thumbnails...":"");
}


/* stop any currently-active idle func for thumbnail reading. */
void stop_thumbnail_read(void)
{
if(thumbnail_read_running())
  {
  gtk_statusbar_pop(GTK_STATUSBAR(statusbar),tn_id);	/* remove msg */
  g_source_remove(tn_idle_tag);
  tn_idle_tag=-1;
  }
}


/* read *currently visible* thumbnails, blocking until all have been read.
 * Don't use this unless you know what you're doing, it can take a while...
 * if checkptr isn't NULL, it aborts if *checkptr becomes NULL.
 */
void blocking_thumbnail_read_visible(GtkWidget **checkptr)
{
int entry;
int row=-1;

if(thumbnail_read_running()) return;

row = first_visible_row();
if(row==-1) return;

idle_xvpic_lastadjval=gtk_adjustment_get_value(gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(treeview)));
idle_xvpic_jumped=0;
entry=row;

while(entry!=-1 && mainwin && (!checkptr || *checkptr) &&
      row_is_visible(entry))
  {
  if(mainwin && (!checkptr || *checkptr))
    do_gtk_stuff();		/* make sure things get updated */
  idle_xvpic_load(&entry);
  }
}


/* do the actual resorting. Also used by rename.c after renaming a file. */
void resort_finish(void)
{
int was_reading=0;
GtkTreePath *path;
GtkTreeRowReference *row_ref;

if(thumbnail_read_running())
  {
  stop_thumbnail_read();
  was_reading=1;
  }

/* set focus row to selection if it exists */
if(current_selection!=-1)
  set_focus_row(current_selection);

/* now we do everything in terms of the focus row.
 */
path=gtk_tree_path_new_from_indices(focus_row, -1);
row_ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(liststore), path);
gtk_tree_path_free(path);

sort_model_rows();

/* look up data, and reselect it. */
if(row_ref)
  {
  path = gtk_tree_row_reference_get_path(row_ref);
  int tmp = get_path_row_number(path);
  gtk_tree_path_free(path);
  
  /* ... get_path_row_number() returns -1 on error, great for this. :-) */
  if(current_selection!=-1)
    current_selection=tmp;
  
  /* not so good for this though. */
  set_focus_row((tmp!=-1)?tmp:0);
  
  /* Hmm. Ok, one thing we have to do which can't be done entirely
   * in terms of the focus row is to move the selection to the right place. :-)
   */
  if(current_selection!=-1)
    {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

    /* block selection handler while selecting it, so we don't reload pic! */
    g_signal_handler_block(selection, cb_selection_id);
    select_row(current_selection);
    g_signal_handler_unblock(selection, cb_selection_id);
    }

  gtk_tree_row_reference_free(row_ref);
  }

/* now deal with visibility problems. This takes the same approach
 * as toggle_thin_rows() - if one selected move focus row to there
 * (did that before the sort), and either way force focus row to middle.
 */
move_to_row(focus_row,0.5);

/* a thumbnail-read may have been ongoing; if so, restart it. */
if(was_reading)
  start_thumbnail_read();
}



void cb_set_order(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data)
{
  filesel_sorttype = gtk_radio_action_get_current_value(current);
  resort_finish();
}

void cb_set_timestamp_type(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data)
{
  sort_timestamp_type = gtk_radio_action_get_current_value(current);
  resort_finish();
}


void cb_next_image(void)
{
int row;

/* since the implicit cb_selection call below checks for GTK+ events,
 * we have to protect against being recursed unexpectedly to avoid
 * segfaults. :-)
 */
if(in_nextprev) return;

if(current_selection==-1) return;	/* skip it if no current selection */
if(current_selection>=numrows-1) return;	/* skip if no next image */

in_nextprev=1;

row=current_selection+1;
make_visible_if_not(row);

/* this causes a cb_selection call, which does the rest,
 * including zeroing in_nextprev.
 */
set_focus_row(row);
select_row(row);	/* sets current_selection */
in_nextprev=0;
}


void cb_prev_image(void)
{
struct row_data_tag *datptr;
int row;

if(in_nextprev) return;

if(current_selection==-1) return;	/* skip it if no current selection */

/* checking for previous image is slightly more complicated.
 * If current_selection is zero there's none, of course;
 * however, there's also none if the previous one is a dir.
 */
if(current_selection==0) return;

datptr=get_row_data(current_selection-1);
if(datptr->isdir) return;

in_nextprev=1;

row=current_selection-1;
make_visible_if_not(row);

/* this causes a cb_selection call, which does the rest,
 * including zeroing in_nextprev.
 */
set_focus_row(row);
select_row(row);	/* sets current_selection */
in_nextprev=0;
}


void cb_tag_then_next(void)
{
/* good idea to check this early */
if(in_nextprev) return;

/* this filters out images left in viewer after dir change,
 * and the case of there being no image in the viewer to tag. :-)
 */
if(current_selection==-1) return;

set_tagged_state(current_selection,1);	/* tag it */

cb_next_image();
}


void cb_help_contents(void)
{
help_run("Top");
}

void cb_help_selector(void)
{
help_run("The File Selector");
}

void cb_help_viewer(void)
{
help_run("The Viewer");
}

void cb_help_index(void)
{
help_run("Concept Index");
}

void cb_help_about(void)
{
help_about();
}



void ditch_line(FILE *in)
{
int c;

while((c=fgetc(in))!='\n' && c!=EOF);
}


/* for text-style PNM files, i.e. P[123].
 * we take an extremely generous outlook - anything other than a decimal
 * digit is considered whitespace.
 * and as per p[bgp]m(5), comments can start anywhere.
 */
int read_next_number(FILE *in)
{
int c,num,in_num,gotnum;

num=0;
in_num=gotnum=0;

do
  {
  if(feof(in)) return(0);
  if((c=fgetc(in))=='#')
    ditch_line(in);
  else
    if(isdigit(c))
      num=10*num+c-49+(in_num=1);
    else
      gotnum=in_num;
  }
while(!gotnum);  

return(num);
}


/* xv 3:3:2 thumbnail files - these are similar to pgm raw files,
 * but the context in which they are used is very different; as such
 * we have a separate routine for loading them.
 * they seem to have a max. size of 80x60.
 */
int read_xvpic(char *filename,unsigned char *bmap,int *width,int *height)
{
FILE *in;
char buf[128];
int w,h,maxval;
int count;

*width=0; *height=0;

if((in=fopen(filename,"rb"))==NULL)
  return(0);

fgets(buf,sizeof(buf),in);
if(strcmp(buf,"P7 332\n")!=0) {
  fclose(in);
  return(0);
}

/* we're not worried about any comments */
w=read_next_number(in);
h=read_next_number(in);

*width=w; *height=h;

if(w==0 || h==0 || w>80 || h>60) {
  fclose(in);
  return(0);
}

/* for some reason, they have a maxval...!?
 * we complain if it's not 255.
 */
if((maxval=read_next_number(in))!=255) {
  fclose(in);
  return(0);
}

count=fread(bmap,1,w*h,in);
if(count!=w*h) {
  fclose(in);
  return(0);
}

fclose(in);
return(1);
}


/* get closest-colour palette
 * XXX will look *awful* on 8-bit, as I'm not dithering!
 * (actually it's not *that* bad, but it's still fairly crap)
 */
void find_xvpic_cols(void)
{
    int r,g,b;
    int n;

    for(n=0,r=0;r<8;r++) {
        for(g=0;g<8;g++) {/* colours are 3:3:2 */
            for(b=0;b<4;b++,n++) {
                xvpic_pal[n][0]=r*0xff/7; 
                xvpic_pal[n][1]=g*0xff/7;
                xvpic_pal[n][2]=b*0xff/3;
            }
        }
    }
}


GdkPixbuf *xvpic2pixbuf(unsigned char *xvpic,int w,int h,GdkPixbuf **smallp)
{
GdkPixbuf *pixbuf,*small_pixbuf;
guint8 *buffer, *small_buffer;
unsigned char *ptr=xvpic;
int x,y;
int small_w,small_h;

if(w==0 || h==0) return(NULL);

small_w=w/ROW_HEIGHT_DIV;
small_h=h/ROW_HEIGHT_DIV;
if(small_w==0) small_w=1;
if(small_h==0) small_h=1;

buffer = malloc (w * h * sizeof (guint8) * 3);

if (NULL == buffer) {
    /* malloc failed */
    return NULL;
}


for(y=0;y<h;y++) {
  for(x=0;x<w;x++) {
      buffer[3*(y*w + x)+0] = xvpic_pal[*ptr][0]; /* red */
      buffer[3*(y*w + x)+1] = xvpic_pal[*ptr][1]; /* green */
      buffer[3*(y*w + x)+2] = xvpic_pal[*ptr][2]; /* blue */
      ptr++;
  }
}

pixbuf = gdk_pixbuf_new_from_data(
    (guchar*)buffer,                 /* data */
    GDK_COLORSPACE_RGB,              /* colorspace */
    FALSE,                           /* has_alpha */
    8,                               /* bits_per_sample */
    w, h,                            /* width, height */
    w * 3,                           /* rowstride */
    (GdkPixbufDestroyNotify)g_free,  /* destroy_fn */
    NULL);                           /* destroy_fn_data */

if (NULL == pixbuf)
  {
    free(buffer);
    return(NULL);
  }

gdk_flush();

/* reuse image to draw scaled-down version for thin rows */

small_buffer = malloc (small_w * small_h * sizeof (guint8) * 3);

for(y=0;y<small_h;y++) {
  for(x=0;x<small_w;x++) {
      small_buffer[3*(y*small_w + x)+0] = xvpic_pal[xvpic[(y*w+x)*ROW_HEIGHT_DIV]][0];
      small_buffer[3*(y*small_w + x)+1] = xvpic_pal[xvpic[(y*w+x)*ROW_HEIGHT_DIV]][1];
      small_buffer[3*(y*small_w + x)+2] = xvpic_pal[xvpic[(y*w+x)*ROW_HEIGHT_DIV]][2];
  }
}

small_pixbuf = gdk_pixbuf_new_from_data(
    (guchar*)small_buffer,           /* data */
    GDK_COLORSPACE_RGB,              /* colorspace */
    FALSE,                           /* has_alpha */
    8,                               /* bits_per_sample */
    small_w, small_h,                /* width, height */
    small_w * 3,                     /* rowstride */
    (GdkPixbufDestroyNotify)g_free,  /* destroy_fn */
    NULL);                           /* destroy_fn_data */

if (NULL == small_pixbuf)
  {
    g_object_unref(pixbuf);
    free(small_buffer);
    return(NULL);
  }

*smallp=small_pixbuf;

return(pixbuf);
}


gint idle_xvpic_load(int *entryp)
{
static char buf[1024];
struct row_data_tag *datptr;
char *ptr;
int f,w,h;
GdkPixbuf *pixbuf,*small_pixbuf;
static unsigned char xvpic_data[80*60];		/* max thumbnail size */
float adjval;
static int prev_scanpos=0;	/* if jumped, saved pos in top-to-bot scan */

idle_xvpic_called=1;

/* don't do it if it would be a bad time */
if(idle_xvpic_blocked)
  return 0;

/* freeze/thaw actually *cause* flickering for this, rather than
 * preventing it (!), so I've not used those here.
 */

adjval=gtk_adjustment_get_value(gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(treeview)));
if(adjval!=idle_xvpic_lastadjval)
  {
  int row=-1;
  
  idle_xvpic_lastadjval=adjval;
  
  /* scrollbar position has changed, jump to first visible row.
   * (We'll make another pass to clean things up later.)
   * This can greatly reduce apparent thumbnail load time for
   * big dirs, even though in practice if you move about a lot
   * it can actually increase it somewhat. :-)
   */
  row = first_visible_row();
  if(row!=-1)
    {
    idle_xvpic_jumped++;
    if(idle_xvpic_jumped==1)
      {
      /* save next one to check after, but only for first
       * jump, not any `recursive' ones.
       */
      prev_scanpos=*entryp;
      }
    *entryp=row;
    }
  }

for(f=0;f<IDLE_XVPIC_NUM_PER_CALL;f++)
  {
  /* if there's already a pixbuf there, skip it. */
  if(!get_row_thumbnails(*entryp, &pixbuf, &small_pixbuf))
    {
    /* construct filename for file's (possible) thumbnail */
    get_row_filename(*entryp,&ptr);
    strcpy(buf,".xvpics/");
    strncat(buf,ptr,sizeof(buf)-8-2);	/* above string is 8 chars long */
    g_free(ptr);
    
    datptr=get_row_data(*entryp);
    
    /* if it's a dir, use ref to dir_icon pixbuf. */
    if(datptr->isdir)
      {
      set_row_thumbnails(*entryp, dir_icon, dir_icon_small);
      }
    else
      {
      /* it's a file, try to load a thumbnail for it */
      if(read_xvpic(buf,xvpic_data,&w,&h) &&
         (pixbuf=xvpic2pixbuf(xvpic_data,w,h,&small_pixbuf))!=NULL)
        {
        set_row_thumbnails(*entryp, pixbuf, small_pixbuf);
        }
      else
        {
        /* no thumbnail then, use ref to file_icon pixbuf. */
        set_row_thumbnails(*entryp, file_icon, file_icon_small);
        }
      }
    }
  
  (*entryp)++;
  
  /* if we jumped, stop on first invisible row or end of list */
  if(idle_xvpic_jumped &&
     (*entryp>=numrows ||
      !row_is_visible(*entryp)))
    {
    /* we pop all jumps, as it were; we only did ..._jumped++ above
     * to ensure we save a single (correct! :-)) prev_scanpos.
     */
    idle_xvpic_jumped=0;
    *entryp=prev_scanpos;
    }
  
  if(*entryp>=numrows)
    {
    /* can't have jump to return from, so just remove ourselves. */
    stop_thumbnail_read();
    *entryp=-1;
    }
  }
return 1;
}


/* remove everything from liststore, freeing pixbufs beforehand */
void blast_liststore(void)
{
int f;
struct row_data_tag *datptr;

if(numrows==0) return;

/* freezing here is not just about efficiency, but also prevents cb_selection()
 * from being called and trying to read the row data which we have just freed
 */
flist_freeze();

/* stop any `currently'-running idle func to read thumbnails
 * (doing this now is probably overly paranoid, but it can't hurt)
 */
stop_thumbnail_read();

for(f=0;f<numrows;f++)
  {
  datptr=get_row_data(f);
  if(datptr)
    free(datptr);
  }

/* now remove all rows at once */
gtk_list_store_clear(liststore);
numrows=0;

/* reset columns width, so that the filename column can grow from zero again */
gtk_tree_view_columns_autosize(GTK_TREE_VIEW(treeview));

focus_row = 0;

flist_thaw();
}


gint sort_cmp(
    GtkTreeModel *model,
    GtkTreeIter  *a,
    GtkTreeIter  *b,
    gpointer      user_data)
{
g_autofree char *txt1 = NULL, *txt2 = NULL;
struct row_data_tag *dat1,*dat2;

gtk_tree_model_get(model, a, MODEL_NAME_COL, &txt1, -1);
gtk_tree_model_get(model, b, MODEL_NAME_COL, &txt2, -1);
gtk_tree_model_get(model, a, MODEL_DATA_COL, &dat1, -1);
gtk_tree_model_get(model, b, MODEL_DATA_COL, &dat2, -1);

/* directories always come first.
 * so, if comparing two files, use a normal comparison;
 * otherwise if it's two dirs, use a strcmp on the names;
 * otherwise it's one file and one dir, and the dir is always `less'.
 */
if(dat1->isdir && dat2->isdir)
  return(strcmp(txt1,txt2));  /* both directories, use strcmp. */

if(!dat1->isdir && !dat2->isdir)
  {
  /* both files, use normal comparison. */
  int ret=0;
  
  switch(filesel_sorttype)
    {
    case sort_name:
      ret=strcmp(txt1,txt2);
      break;
    
    case sort_ext:
      ret=strcmp(txt1+dat1->extofs,txt2+dat2->extofs);
      break;
    
    case sort_size:
      if(dat1->size<dat2->size)
        ret=-1;
      else
        if(dat1->size>dat2->size)
          ret=1;
      break;
    
    case sort_time:
      {
      time_t t1,t2;

      switch(sort_timestamp_type)
        {
        default: t1=dat1->mtime; t2=dat2->mtime; break;
        case 1:  t1=dat1->ctime; t2=dat2->ctime; break;
        case 2:  t1=dat1->atime; t2=dat2->atime; break;
        }
      
      if(t1<t2)
        ret=-1;
      else
        if(t1>t2)
          ret=1;
      break;
      }
    }
  
  /* for all equal matches on primary key, use name as secondary */
  if(ret==0)
    ret=strcmp(txt1,txt2);
  
  return(ret);
  }	/* end of if */

/* otherwise, one or both are dirs. */

if(dat1->isdir) return(-1);	/* first one is dir */
return(1);			/* else second one is dir */
}


int add_new_row(char *filename,struct stat *sbuf)
{
struct row_data_tag *datptr;
char *ptr;
GtkTreeIter iter;
static char* extensions[] ={".GIF", ".JPEG", ".JPG", ".PNG", ".PBM", ".PGM", ".PPM",
                            ".PNM", ".BMP",  ".TGA", ".PCX", ".MRF", ".PRF", ".XBM",
                            ".XPM", ".TIFF", ".TIF", ".TIM", ".XWD"};

if (!S_ISDIR(sbuf->st_mode) && show_images_only)
    {
    int isImage = 0;
    int i;
    gchar* nameUpper = g_ascii_strup(filename, -1);
    for(i = 0; i < 19; ++i)
      {
      if(g_str_has_suffix(nameUpper, extensions[i]))
        {
        isImage = 1;
        break;
        }
      }
    g_free(nameUpper);
    if (!isImage)
      return(0);
    }

/* allocate data-pointer struct for row */
if((datptr=malloc(sizeof(struct row_data_tag)))==NULL)
  return(0);

/* can't use a pointer to the extension (GTK+ makes its own copy
 * of the filename), so has to be an offset.
 */
if((ptr=strrchr(filename,'.'))==NULL)
  /* use the NUL, Luke */
  datptr->extofs=strlen(filename);
else
  datptr->extofs=ptr-filename;

datptr->isdir=S_ISDIR(sbuf->st_mode);
datptr->size=sbuf->st_size;
datptr->mtime=sbuf->st_mtime;
datptr->ctime=sbuf->st_ctime;
datptr->atime=sbuf->st_atime;
datptr->tagged=0;

gtk_list_store_append(liststore, &iter);
gtk_list_store_set(liststore, &iter,
    MODEL_NAME_COL, filename,
    MODEL_DATA_COL, datptr,
    -1);

/* we *could* put pixbufs in place for directories right now,
 * rather than waiting for idle_xvpic_load() to do it. However,
 * this a) seems to end up being a bit flickery despite the list
 * being `frozen', and b) looks rather odd. :-)
 */

return(1);
}


/* read filenames from current dir, and (eventually) load thumbnails.
 *
 * Note that this actually just sets up the filenames, and enables
 * an idle function which loads the xvpics (removing any already-running
 * one to do this, if needed).
 */
void add_new_rows_from_dir(void)
{
DIR *dirfile;
struct dirent *dent;
struct stat sbuf;
static char cdir[1024];

if((dirfile=opendir("."))==NULL)
  {
  /* we get here if we can't read the dir.
   * xzgv tests we have permission to access a file or dir before
   * selecting it, so this can only happen if it was started on the dir
   * from the cmdline, or if the directory has changed somehow since we
   * last read it.
   * the first reaction is to try $HOME instead.
   * if *that* doesn't work, we cough and die, not unreasonably. :-)
   */
  /* be sure to mention what we're doing first... :-) */
  if(getenv("HOME")==NULL)
    goto badhome;
  xzgv_chdir(getenv("HOME"));
  if((dirfile=opendir("."))==NULL)
    {
    badhome:
    fprintf(stderr,
            "xzgv: $HOME is unreadable or not set. "
            "This is a Bad Thing. TTFN...\n");
    exit(1);
    }
  
  error_dialog("xzgv warning",
               "Directory unreadable - jumped to home dir instead");
  
  /* if moving to $HOME worked, may need to change dir in title `by hand'... */
  set_title(1);
  }

current_selection=-1;

xzgv_getcwd(cdir,sizeof(cdir)-1);

/* originally had a `reading directory' msg here, but it's so fast
 * even for big dirs that it hardly seems worth it.
 */

/* remove any currently-running idle func */
stop_thumbnail_read();

flist_freeze();

numrows=0;
while((dent=readdir(dirfile))!=NULL)
  {
  if(dent->d_name[0]=='.' && dent->d_name[1]!='.')
    continue;				/* skip (most) 'dot' files */
  
  /* no `.' ever, and no `..' if at root. */
  if(strcmp(dent->d_name,".")==0 ||
     (strcmp(cdir,"/")==0 && strcmp(dent->d_name,"..")==0))
    continue;
  
  /* see if it's a dir */
  if((stat(dent->d_name,&sbuf))==-1)
    {
    sbuf.st_mode=0;
    sbuf.st_size=0;
    sbuf.st_mtime=0;
    sbuf.st_ctime=0;
    sbuf.st_atime=0;
    }
  
  if(add_new_row(dent->d_name,&sbuf))
    numrows++;
  }

closedir(dirfile);

if(numrows)
  {
  /* sort the list (using sort_cmp) */
  sort_model_rows();
  
  /* unselect the first row to give us a sane initial pos for
   * keyboard movement. (Doesn't seem to be necessary after sorting,
   * but can't hurt.)
   */
  unselect_row(0);
  
  /* setup idle function to load thumbnails. */
  start_thumbnail_read();
  }

flist_thaw();
}


void set_title(int include_dir)
{
static char buf[1024];

strcpy(buf,"xzgv");
if(include_dir)
  {
  strcat(buf,": ");
  xzgv_getcwd(buf+strlen(buf),sizeof(buf)-strlen(buf)-2);
  }

gtk_window_set_title(GTK_WINDOW(mainwin),buf);
}


/* add new pastpos[0], shifting down all the rest of the entries. */
void new_pastpos(int row)
{
struct stat sbuf;
int f;

for(f=MAX_PASTPOS-1;f>0;f--)
  {
  pastpos[f].dev  =pastpos[f-1].dev;
  pastpos[f].inode=pastpos[f-1].inode;
  pastpos[f].row  =pastpos[f-1].row;
  }

if(stat(".",&sbuf)==-1) return;

pastpos[0].dev  =sbuf.st_dev;
pastpos[0].inode=sbuf.st_ino;
pastpos[0].row  =row;
}


/* return row from pastpos[] entry matching current directory,
 * or if none match return 0.
 */
int get_pastpos(void)
{
struct stat sbuf;
int f;

if(stat(".",&sbuf)==-1) return(0);

for(f=0;f<MAX_PASTPOS;f++)
  if(pastpos[f].inode==sbuf.st_ino && pastpos[f].dev==sbuf.st_dev)
    return(pastpos[f].row);

return(0);
}


/* gives a simple modal dialog box with a label containing an error message.
 * It returns right after creating it, but since it's modal, it shouldn't
 * need any further consideration.
 */
void error_dialog(char *title,char *msg)
{
GtkWidget *error_win;
GtkWidget *vbox,*label,*action_tbl,*button;

error_win=gtk_dialog_new();

/* make a new vbox for the top part so we can get spacing more sane */
vbox=gtk_vbox_new(FALSE,10);
gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(error_win))),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(gtk_dialog_get_action_area(GTK_DIALOG(error_win))),2);

gtk_window_set_title(GTK_WINDOW(error_win),title);
gtk_window_set_resizable(GTK_WINDOW(error_win), TRUE);
gtk_window_set_position(GTK_WINDOW(error_win),GTK_WIN_POS_CENTER);
gtk_window_set_modal(GTK_WINDOW(error_win),TRUE);

label=gtk_label_new(msg);
gtk_box_pack_start(GTK_BOX(vbox),label,TRUE,TRUE,2);
gtk_widget_show(label);

/* add ok button */
action_tbl=gtk_table_new(1,3,TRUE);
gtk_box_pack_start(GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(error_win))),
                   action_tbl,TRUE,TRUE,0);
gtk_widget_show(action_tbl);

button=gtk_button_new_with_label("Ok");
gtk_table_attach_defaults(GTK_TABLE(action_tbl),button, 1,2, 0,1);
g_signal_connect_swapped(button, "clicked",
                          G_CALLBACK(gtk_widget_destroy),
                          error_win);
gtk_widget_grab_focus(button);
gtk_widget_show(button);

/* also allow escs (even from main window!) */
gtk_widget_add_accelerator(button,"clicked",mainwin_accel_group,
                           GDK_KEY_Escape,0,0);


gtk_widget_show(error_win);
}


/* close file (clear viewer). */
void cb_file_close(void)
{
unselect_all();
current_selection=-1;
cb_back_to_flist();		/* enable selector */

if(theimage)
{
  backend_image_destroy(theimage);
  theimage=NULL;
}

/* ignore revert/revert_orient for this */
xscaling=yscaling=1;
orient_current_state=0;
}


void cb_delete_file_confirmed(void)
{
static char *prefix=".xvpics/";
g_autofree char *ptr = NULL;
char *tn;
int row;
int was_reading=0;
GtkTreeIter iter;

row=focus_row;
get_row_filename(row,&ptr);

/* delete the file */
if(remove(ptr)==-1)
  {
  error_dialog("xzgv error","Unable to delete file!");
  return;
  }

cb_back_to_flist();

/* construct thumbnail filename early, as we're about to delete
 * the row containing the filename itself.
 */
tn=malloc(strlen(prefix)+strlen(ptr)+1);
if(tn)
  strcpy(tn,prefix),strcat(tn,ptr);

/* remove the row in the liststore. We need to stop/restart thumbnail read
 * if it's running, as unexpectedly losing a row midway through could
 * cause problems.
 */
if(thumbnail_read_running())
  {
  stop_thumbnail_read();
  was_reading=1;
  }

gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore), &iter, NULL, row);
gtk_list_store_remove(liststore, &iter);
numrows--;

if(was_reading)
  start_thumbnail_read();

/* the current row could have been the selected one; correct our notion
 * of the selected file if so.
 * (XXX could also automatically `close' the file, but that could be
 * somewhat disturbing visually...?)
 */
if(current_selection==row)
  current_selection=-1;

/* if we deleted the last row, move the focus row to the new last row */
if (row == numrows)
  focus_row--;

/* only now do we quit if we couldn't allocate mem for tn */
if(!tn) return;

remove(tn);		/* don't care if this fails */
rmdir(".xvpics");	/* same here */

free(tn);
}


void cb_delete_file(void)
{
static char *prefix="Really delete `",*suffix="'?";
struct row_data_tag *datptr;
g_autofree char *ptr = NULL;
char *msg;
int row;

row=focus_row;
if(row<0 || row>=numrows) return;

get_row_filename(row,&ptr);
if(!ptr) return;

datptr=get_row_data(row);
if(!datptr || datptr->isdir) return;

msg=malloc(strlen(ptr)+strlen(prefix)+strlen(suffix)+1);
if(!msg) return;

strcpy(msg,prefix);
strcat(msg,ptr);
strcat(msg,suffix);

/* ok, check if they're sure. If so, the above callback routine
 * will be called.
 */
if(delete_single_prompt)
  confirmation_dialog("Delete File",msg,cb_delete_file_confirmed);
else
  cb_delete_file_confirmed();

free(msg);
}


void reinit_dir(int do_pastpos,int try_to_save_cursor_pos)
{
int row;
g_autofree char *ptr = NULL;
char *oldname=NULL;

if(do_pastpos && try_to_save_cursor_pos)
  fprintf(stderr,"xzgv: both args to reinit_dir() set, bug alert :-)\n"),
    try_to_save_cursor_pos=0;

if(try_to_save_cursor_pos)
  {
  get_row_filename(focus_row,&ptr);
  if(!ptr || (oldname=malloc(strlen(ptr)+1))==NULL)
    try_to_save_cursor_pos=0;
  else
    strcpy(oldname,ptr);
  }

blast_liststore();
add_new_rows_from_dir();
set_title(1);

if(try_to_save_cursor_pos)
  {
  int f;

  for(f=0;f<numrows;f++)
    {
    get_row_filename(f,&ptr);
    if(*ptr==*oldname && strcmp(ptr,oldname)==0)
      {
      /* focus and make sure it's visible */
      set_focus_row(f);
      make_visible_if_not(f);
      break;
      }
    }

  free(oldname);
  }

if(do_pastpos)
  {
  row=get_pastpos();
  
  if(row<numrows)
    {
    /* don't select old row, that would be annoying. Just focus it, and
     * put it in middle of win.
     */
    set_focus_row(row);
    move_to_row(row,0.5);
    }
  }
}


void cb_reread_dir(void)
{
reinit_dir(0,1);		/* reread, don't do pastpos, save cursor pos */
}


void cb_copy_files(void)
{
cb_back_to_flist();
cb_copymove_file_or_tagged_files(0);
}


void cb_move_files(void)
{
cb_back_to_flist();
cb_copymove_file_or_tagged_files(1);
}


/* block keyboard/mouse input to selector */
void selector_block(void)
{
/* can't do this with g_signal_handler_block, as that doesn't block
 * the native treeview handlers. Need to still have the handlers, but
 * have them actively ignore the events.
 */
ignore_selector_input=1;
}

/* and unblock it */
void selector_unblock(void)
{
ignore_selector_input=0;
}


void cb_selection(GtkTreeSelection *selection,
                  GtkScrolledWindow *sw)
{
g_autofree char *ptr = NULL;
xzgv_image *oldimage=theimage;
struct row_data_tag *datptr;
int orient_lastpicexit_state=0;
int old_selection=current_selection;
int row;
GtkTreeIter iter;
GtkTreePath *path;
FILE *test;
static int in_routine=0;

/* guard against recursion (from GTK+ updates) */
if(in_routine) return;

in_routine=1;

/* block mouse click/release and keys on selector while loading. */
selector_block();

if (!gtk_tree_selection_get_selected(selection, NULL, &iter))
{
  /* no row selected */
  selector_unblock();
  in_nextprev=in_routine=0;
  return;
}
path = gtk_tree_model_get_path(GTK_TREE_MODEL(liststore), &iter);
row = get_path_row_number(path);
gtk_tree_path_free(path);

current_selection=row;
set_focus_row(row);

get_row_filename(row,&ptr);

/* don't think this can happen, but what the heck */
if(!ptr)
  {
  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

/* this definitely can't be NULL; always allocated if the row exists */
datptr=get_row_data(row);

if(!datptr)	/* but it can't hurt to check :-) */
  {
  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

/* see if the file (or dir) exists and we have permission to read it.
 * By, um, trying to open it. :-) (I was going to use access(2) to do
 * a better check than this, but apparently that's a bad idea?)
 */
if((test=fopen(ptr,"rb"))!=NULL)
  fclose(test);
else
  {
  /* nope; say so */
  error_dialog("xzgv error","Permission denied or file not found");
  
  /* restore old selection state */
  current_selection=old_selection;
  if(current_selection==-1)
    {
    /* unselect, then */
    unselect_all();
    }
  else	    /* a previous file was selected, reselect it (but don't reload) */
    {
    /* block selection handler while selecting it, so we don't reload pic! */
    g_signal_handler_block(selection, cb_selection_id);
    select_row(current_selection);
    g_signal_handler_unblock(selection, cb_selection_id);
    }

  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

if(datptr->isdir)
  {
  /* if it's a dir, chdir to it and read files there instead. */
  cb_back_to_flist();	/* in case of mouse click, to show pastpos action */
  new_pastpos(row);
  xzgv_chdir(ptr);
  reinit_dir(1,0);	/* reinit and do pastpos */

  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

gtk_statusbar_push(GTK_STATUSBAR(statusbar),sel_id,"Reading file...");
/* let GTK+ show it */
do_gtk_stuff();

if((theimage=load_image(ptr,0,NULL,NULL))==NULL)
  {
  gtk_statusbar_pop(GTK_STATUSBAR(statusbar),sel_id);
  
  theimage=oldimage;	/* keep hold of the old one */
  
  error_dialog("xzgv error","Couldn't load image!");
  
  /* also enable the selector; the assumption is that this is nicer
   * than leaving them with a blank window (if they ran it on pics
   * from the command-line). :-) There doesn't seem any point
   * keeping focus on the image, anyway, especially as (in the
   * case of the first command-line pic failing to load) there
   * may not even *be* an image. (Also, it makes it very obvious
   * which file screwed up.)
   */
  cb_back_to_flist();	/* enable selector */
  
  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

/* reflect loading of new pic in orientation stuff */
orient_lastpicexit_state=orient_current_state;
orient_current_state=0;

if(use_exif_orient)
  {
  /* apply Exif orientation correction, then pretend it's the normal pic */
  orient_change_state(0,jpeg_exif_orient);
  orient_current_state=0;
  }

if(revert)
  {
  xscaling=yscaling=1;
  /* note that revert *doesn't* do interp=0 in xzgv (it does in zgv) */
  /* XXX should mention this in docs, may confuse zgv refugees :-) */
  }

if(revert_orient)
  {
  /* if the last state was rotated, need to swap over scales. */
  if(orient_lastpicexit_state>3)
    swap_xyscaling();
  }
else
  {
  /* since we're effectively *restoring* the state, we need to
   * preserve x/yscaling which will be erroneously `corrected'.
   */
  int xsav=xscaling,ysav=yscaling;
  
  orient_change_state(orient_current_state,orient_lastpicexit_state);
  orient_current_state=orient_lastpicexit_state;
  xscaling=xsav; yscaling=ysav;
  }

/* don't pointlessly render a zoomed copy if we're just about to resize
 * it due to auto-hide!
 */
if(!zoom || !auto_hide || hidden)
  render_pixbuf(1);

/* only now can we safely free the old image (any old pixbuf
 * will now no longer be onscreen).
 */
if(oldimage)
  backend_image_destroy(oldimage),oldimage=NULL;

gtk_statusbar_pop(GTK_STATUSBAR(statusbar),sel_id);

/* switch focus to pic */
gtk_widget_grab_focus(eb_for_pic);

/* stop us allowing kybd focus (until esc/tab) */
gtk_widget_set_can_focus(flist_sw_ebox, FALSE);

/* hide us if auto hide is on */
if(auto_hide && !hidden)
  cb_hide_selector();

/* let them use the selector again :-) */
selector_unblock();

/* allow next/prev and calls to this again */
in_nextprev=in_routine=0;
}


void set_window_pos_and_size(void)
{
if(fullscreen)
  {
  /* go to top-left and use full screen */
  /* Note: This is *not* identical to gtk_window_fullscreen() */
  gtk_window_move(GTK_WINDOW(mainwin),0,0);
  gtk_window_set_default_size(GTK_WINDOW(mainwin),
                              gdk_screen_width(),gdk_screen_height());
  }
else	/* normal */
  {
  if((mainwin_flags&GEOM_BITS_X_SET) &&
     (mainwin_flags&GEOM_BITS_Y_SET))
    gtk_window_move(GTK_WINDOW(mainwin),mainwin_x,mainwin_y);
  /* we always have width/height set */
  gtk_window_set_default_size(GTK_WINDOW(mainwin),mainwin_w,mainwin_h);
  }
}


void init_window(void)
{
/* basic layout is like this:
 *   (paned in window contains all this)
 *   __________________paned_________________
 * v|                |^|                     | 	maybe toolbar here eventually?
 * b|list of pics    |||                     |
 * o| in scrolled win|||                     |
 * x|1st col xvpic,  ||| pic in scrolled win |
 * l|2nd col fname.  |||                     |
 *  |                |v|                     |
 *  |------------------|                     |
 *  |status bar        |                     |
 *  `----------------------------------------'
 */
GtkWidget *vboxl;
GtkUIManager *ui_manager;
GdkPixbuf *icon;
char *ptr;
GtkAllocation allocation, allocation2;
GtkCellRenderer *renderer;
GtkTreeSelection *selection;

GtkActionEntry selector_menu_entries[] = {
  { "UpdateTN",          NULL, "_Update Thumbnails", "u",      NULL, G_CALLBACK(cb_update_tn) },
  { "UpdateTNRecursive", NULL, "_Recursive Update",  "<alt>u", NULL, G_CALLBACK(cb_update_tn_recursive) },

  { "FileMenu", NULL, "_File" },
  { "Open",     NULL, "_Open",           NULL,         NULL, G_CALLBACK(view_focus_row_file) },
  { "Details",  NULL, "_Details...",     "colon",      NULL, G_CALLBACK(cb_file_details) },
  { "Close",    NULL, "Clo_se",          "<control>w", NULL, G_CALLBACK(cb_file_close) },
  { "Copy",     NULL, "_Copy...",        "<shift>c",   NULL, G_CALLBACK(cb_copy_files) },
  { "Move",     NULL, "_Move...",        "<shift>m",   NULL, G_CALLBACK(cb_move_files) },
  { "Rename",   NULL, "_Rename file...", "<control>n", NULL, G_CALLBACK(cb_rename_file) },
  { "Delete",   NULL, "De_lete file...", "<control>d", NULL, G_CALLBACK(cb_delete_file) },

  { "TaggingMenu",  NULL, "_Tagging" },
  { "sNextTagged",  NULL, "_Next Tagged",     "slash",      NULL, G_CALLBACK(cb_selector_next_tagged) },
  { "sPrevTagged",  NULL, "_Previous Tagged", "question",   NULL, G_CALLBACK(cb_selector_prev_tagged) },
  { "Tag",          NULL, "_Tag",             "equal",      NULL, G_CALLBACK(cb_tag_file) },
  { "Untag",        NULL, "_Untag",           "minus",      NULL, G_CALLBACK(cb_untag_file) },
  { "TagAll",       NULL, "Tag _All",         "<alt>equal", NULL, G_CALLBACK(cb_tag_all) },
  { "UntagAll",     NULL, "U_ntag All",       "<alt>minus", NULL, G_CALLBACK(cb_untag_all) },
  { "ToggleAll",    NULL, "T_oggle All",      "<alt>o",     NULL, G_CALLBACK(cb_toggle_all) },

  { "DirectoryMenu", NULL, "_Directory" },
  { "ChangeDir",     NULL, "_Change...", "<shift>g",   NULL, G_CALLBACK(cb_goto_dir) },
  { "RescanDir",     NULL, "_Rescan",    "<control>r", NULL, G_CALLBACK(cb_reread_dir) },

  { "DatetimeTypeMenu", NULL, "Time & Date _Type" },

  { "sOptionsMenu", NULL, "_Options" },

  { "HelpMenu",     NULL, "_Help" },
  { "HelpContents", NULL, "_Contents",          "F1", NULL, G_CALLBACK(cb_help_contents) },
  { "HelpSelector", NULL, "The _File Selector", NULL, NULL, G_CALLBACK(cb_help_selector) },
  { "HelpIndex",    NULL, "_Index",             NULL, NULL, G_CALLBACK(cb_help_index) },
  { "About",        NULL, "_About...",          NULL, NULL, G_CALLBACK(cb_help_about) },

  { "Exit", NULL, "E_xit xzgv", "<control>q", NULL, gtk_main_quit }
};

GtkToggleActionEntry selector_menu_toggle_entries[] = {
  { "ImagesOnly",    NULL, "_Images Only",    "<alt>i", NULL, G_CALLBACK(cb_show_images),   FALSE },
  { "AutoHide",      NULL, "_Auto Hide",      "<alt>a", NULL, G_CALLBACK(toggle_auto_hide), FALSE },
  { "StatusBar",     NULL, "_Status Bar",     "<alt>b", NULL, G_CALLBACK(toggle_status),    FALSE },
  { "ThumbnailMsgs", NULL, "Thumb_nail Msgs", NULL,     NULL, G_CALLBACK(toggle_tn_msgs),   FALSE },
  { "ThinRows",      NULL, "_Thin Rows",      "v",      NULL, G_CALLBACK(toggle_thin_rows), FALSE }
};

GtkRadioActionEntry selector_menu_sort_radio_entries[] = {
  { "SortByName",  NULL, "Sort by _Name",        "<alt>n", NULL, sort_name },
  { "SortByExt",   NULL, "Sort by _Extension",   "<alt>e", NULL, sort_ext },
  { "SortBySize",  NULL, "Sort by _Size",        "<alt>s", NULL, sort_size },
  { "SortByTime",  NULL, "Sort by Time & _Date", "<alt>d", NULL, sort_time }
};

GtkRadioActionEntry selector_menu_datetime_radio_entries[] = {
  { "DatetimeMtime", NULL, "_Modification Time (mtime)",     "<alt><shift>m", NULL, 0 },
  { "DatetimeCtime", NULL, "Attribute _Change Time (ctime)", "<alt><shift>c", NULL, 1 },
  { "DatetimeAtime", NULL, "_Access Time (atime)",           "<alt><shift>a", NULL, 2 }
};

char *selector_menu_ui =
"<ui>"
"  <popup name='SelectorMenu' accelerators='true'>"
"    <menuitem action='UpdateTN' />"
"    <menuitem action='UpdateTNRecursive' />"
"    <separator />"
"    <menu action='FileMenu'>"
"      <menuitem action='Open' />"
"      <menuitem action='Details' />"
"      <menuitem action='Close' />"
"      <separator />"
"      <menuitem action='Copy' />"
"      <menuitem action='Move' />"
"      <menuitem action='Rename' />"
"      <menuitem action='Delete' />"
"      <separator />"
"      <!-- duplicate exit, as people will expect it here -->"
"      <menuitem action='Exit' />"
"    </menu>"
"    <menu action='TaggingMenu'>"
"      <menuitem action='sNextTagged' />"
"      <menuitem action='sPrevTagged' />"
"      <separator />"
"      <menuitem action='Tag' />"
"      <menuitem action='Untag' />"
"      <separator />"
"      <menuitem action='TagAll' />"
"      <menuitem action='UntagAll' />"
"      <menuitem action='ToggleAll' />"
"    </menu>"
"    <menu action='DirectoryMenu'>"
"      <menuitem action='ChangeDir' />"
"      <menuitem action='RescanDir' />"
"      <separator />"
"      <menuitem action='ImagesOnly' />"
"      <separator />"
"      <menuitem action='SortByName' />"
"      <menuitem action='SortByExt' />"
"      <menuitem action='SortBySize' />"
"      <menuitem action='SortByTime' />"
"      <menu action='DatetimeTypeMenu'>"
"        <menuitem action='DatetimeMtime' />"
"        <menuitem action='DatetimeCtime' />"
"        <menuitem action='DatetimeAtime' />"
"      </menu>"
"    </menu>"
"    <menu action='sOptionsMenu'>"
"      <menuitem action='AutoHide' />"
"      <menuitem action='StatusBar' />"
"      <menuitem action='ThumbnailMsgs' />"
"      <menuitem action='ThinRows' />"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='HelpContents' />"
"      <menuitem action='HelpSelector' />"
"      <menuitem action='HelpIndex' />"
"      <separator />"
"      <menuitem action='About' />"
"    </menu>"
"    <menuitem action='Exit' />"
"  </popup>"
"</ui>";


GtkActionEntry viewer_menu_entries[] = {

  { "NextImage", NULL, "_Next Image",     "space", NULL, G_CALLBACK(cb_next_image) },
  { "PrevImage", NULL, "_Previous Image", "b",     NULL, G_CALLBACK(cb_prev_image) },

  { "TaggingMenu", NULL, "_Tagging" },
  { "TagThenNext", NULL, "_Tag then Next",   "<control>space", NULL, G_CALLBACK(cb_tag_then_next) },
  { "vNextTagged", NULL, "_Next Tagged",     "slash",          NULL, G_CALLBACK(cb_viewer_next_tagged) },
  { "vPrevTagged", NULL, "_Previous Tagged", "question",       NULL, G_CALLBACK(cb_viewer_prev_tagged) },

  { "ScalingMenu",   NULL, "_Scaling" },
  { "NormalScaling", NULL, "_Normal",             "n",        NULL, G_CALLBACK(cb_normal) },
  { "DoubleScaling", NULL, "_Double Scaling",     "d",        NULL, G_CALLBACK(cb_scaling_double) },
  { "HalveScaling",  NULL, "_Halve Scaling",      "<shift>d", NULL, G_CALLBACK(cb_scaling_halve) },
  { "AddScaling",    NULL, "_Add 1 to Scaling",   "s",        NULL, G_CALLBACK(cb_scaling_add) },
  { "SubScaling",    NULL, "_Sub 1 from Scaling", "<shift>s", NULL, G_CALLBACK(cb_scaling_sub) },

  { "XScalingMenu",   NULL, "_X Only" },
  { "DoubleXScaling", NULL, "_Double Scaling",     "x",             NULL, G_CALLBACK(cb_xscaling_double) },
  { "HalveXScaling",  NULL, "_Halve Scaling",      "<shift>x",      NULL, G_CALLBACK(cb_xscaling_halve) },
  { "AddXScaling",    NULL, "_Add 1 to Scaling",   "<alt>x",        NULL, G_CALLBACK(cb_xscaling_add) },
  { "SubXScaling",    NULL, "_Sub 1 from Scaling", "<alt><shift>x", NULL, G_CALLBACK(cb_xscaling_sub) },
  { "YScalingMenu",   NULL, "_Y Only" },
  { "DoubleYScaling", NULL, "_Double Scaling",     "y",             NULL, G_CALLBACK(cb_yscaling_double) },
  { "HalveYScaling",  NULL, "_Halve Scaling",      "<shift>y",      NULL, G_CALLBACK(cb_yscaling_halve) },
  { "AddYScaling",    NULL, "_Add 1 to Scaling",   "<alt>y",        NULL, G_CALLBACK(cb_yscaling_add) },
  { "SubYScaling",    NULL, "_Sub 1 from Scaling", "<alt><shift>y", NULL, G_CALLBACK(cb_yscaling_sub) },

  { "OrientationMenu", NULL, "O_rientation" },
  { "Normal",          NULL, "_Normal",         "<shift>n", NULL, G_CALLBACK(cb_normal_orient) },
  { "Mirror",          NULL, "_Mirror (horiz)", "m",        NULL, G_CALLBACK(cb_mirror) },
  { "Flip",            NULL, "_Flip (vert)",    "f",        NULL, G_CALLBACK(cb_flip) },
  { "RotateRight",     NULL, "_Rotate Right",   "r",        NULL, G_CALLBACK(cb_rot_cw) },
  { "RotateLeft",      NULL, "Rotate _Left",    "<shift>r", NULL, G_CALLBACK(cb_rot_acw) },

  { "WindowMenu",   NULL, "_Window" },
  { "HideSelector", NULL, "_Hide Selector", "<shift>z",   NULL, G_CALLBACK(cb_hide_selector) },
  { "Minimize",     NULL, "_Minimize",      "<control>z", NULL, G_CALLBACK(cb_iconify) },

  { "vOptionsMenu",   NULL, "_Options" },

  { "HelpMenu",     NULL, "_Help" },
  { "HelpContents", NULL, "_Contents",   "F1",  NULL, G_CALLBACK(cb_help_contents) },
  { "HelpViewer",   NULL, "The _Viewer", NULL,  NULL, G_CALLBACK(cb_help_viewer) },
  { "HelpIndex",    NULL, "_Index",      NULL,  NULL, G_CALLBACK(cb_help_index) },
  { "About",        NULL, "_About...",   NULL,  NULL, G_CALLBACK(cb_help_about) },

  { "ExitToSelector", NULL, "E_xit to Selector", "Escape", NULL, G_CALLBACK(cb_back_to_flist) }
};

GtkToggleActionEntry viewer_menu_toggle_entries[] = {
  { "Zoom",          NULL, "_Zoom (fit to window)",       "z",        NULL, G_CALLBACK(toggle_zoom),          FALSE },
  { "ReduceOnly",    NULL, "When Zooming _Reduce Only",   "<alt>r",   NULL, G_CALLBACK(toggle_zoom_reduce),   FALSE },
  { "Panorama",      NULL, "When Zooming _Panorama",      "<alt>p",   NULL, G_CALLBACK(toggle_zoom_panorama), FALSE },
  { "Interpolate",   NULL, "_Interpolate when Scaling",   "i",        NULL, G_CALLBACK(toggle_interp),        FALSE },
  { "MouseX",        NULL, "_Ctl+Click Scales X Axis",    "<alt>c",   NULL, G_CALLBACK(toggle_mouse_x),       FALSE },
  { "UseExif",       NULL, "Use _Exif Orientation",       NULL,       NULL, G_CALLBACK(toggle_exif_orient),   FALSE },
  { "RevertScaling", NULL, "Revert _Scaling For New Pic", NULL,       NULL, G_CALLBACK(toggle_revert),        FALSE },
  { "RevertOrient",  NULL, "Revert _Orient. For New Pic", NULL,       NULL, G_CALLBACK(toggle_revert_orient), FALSE }
};

char *viewer_menu_ui =
"<ui>"
"  <popup name='ViewerMenu' accelerators='true'>"
"    <menuitem action='NextImage' />"
"    <menuitem action='PrevImage' />"
"    <separator />"
"    <menu action='TaggingMenu'>"
"      <menuitem action='TagThenNext' />"
"      <separator />"
"      <menuitem action='vNextTagged' />"
"      <menuitem action='vPrevTagged' />"
"    </menu>"
"    <menu action='ScalingMenu'>"
"      <menuitem action='NormalScaling' />"
"      <menuitem action='DoubleScaling' />"
"      <menuitem action='HalveScaling' />"
"      <menuitem action='AddScaling' />"
"      <menuitem action='SubScaling' />"
"      <separator />"
"      <menu action='XScalingMenu'>"
"        <menuitem action='DoubleXScaling' />"
"        <menuitem action='HalveXScaling' />"
"        <menuitem action='AddXScaling' />"
"        <menuitem action='SubXScaling' />"
"      </menu>"
"      <menu action='YScalingMenu'>"
"        <menuitem action='DoubleYScaling' />"
"        <menuitem action='HalveYScaling' />"
"        <menuitem action='AddYScaling' />"
"        <menuitem action='SubYScaling' />"
"      </menu>"
"    </menu>"
"    <menu action='OrientationMenu'>"
"      <menuitem action='Normal' />"
"      <menuitem action='Mirror' />"
"      <menuitem action='Flip' />"
"      <menuitem action='RotateRight' />"
"      <menuitem action='RotateLeft' />"
"    </menu>"
"    <menu action='WindowMenu'>"
"      <menuitem action='HideSelector' />"
"      <menuitem action='Minimize' />"
"    </menu>"
"    <menu action='vOptionsMenu'>"
"      <menuitem action='Zoom' />"
"      <menuitem action='ReduceOnly' />"
"      <menuitem action='Panorama' />"
"      <menuitem action='Interpolate' />"
"      <menuitem action='MouseX' />"
"      <menuitem action='UseExif' />"
"      <separator />"
"      <menuitem action='RevertScaling' />"
"      <menuitem action='RevertOrient' />"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='HelpContents' />"
"      <menuitem action='HelpViewer' />"
"      <menuitem action='HelpIndex' />"
"      <separator />"
"      <menuitem action='About' />"
"    </menu>"
"    <separator />"
"    <menuitem action='ExitToSelector' />"
"  </popup>"
"</ui>";


ui_manager = gtk_ui_manager_new();


mainwin=gtk_window_new(GTK_WINDOW_TOPLEVEL);
gtk_widget_set_can_focus(mainwin, TRUE);
g_signal_connect(mainwin, "destroy",
                   G_CALLBACK(cb_quit), NULL);
/* don't include dir if selector initially hidden (loading from cmdline) */
set_title(!hidden);

set_window_pos_and_size();

/* add keys to window */
mainwin_accel_group = gtk_ui_manager_get_accel_group(ui_manager);
gtk_window_add_accel_group(GTK_WINDOW(mainwin),mainwin_accel_group);


pane=gtk_hpaned_new();
gtk_widget_set_can_focus(pane, TRUE);
gtk_container_add(GTK_CONTAINER(mainwin),pane);
gtk_widget_show(pane);


/* right-hand side */

/* the drawing area used for the pic */
image_widget=gtk_image_new();
eb_for_pic = gtk_event_box_new();
gtk_container_add(GTK_CONTAINER(eb_for_pic), image_widget);
gtk_widget_set_can_focus(eb_for_pic, TRUE);
viewer_menu = make_menu(ui_manager,
    "ViewerMenu",
    viewer_menu_ui,
    viewer_menu_entries, G_N_ELEMENTS(viewer_menu_entries),
    viewer_menu_toggle_entries, G_N_ELEMENTS(viewer_menu_toggle_entries),
    NULL, 0, NULL,
    NULL, 0, NULL
    );

#if HAVE_DRAG_GESTURES
GtkGesture *gesture = gtk_gesture_drag_new(eb_for_pic);
gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
g_signal_connect(gesture, "drag-update",
                 G_CALLBACK(on_viewer_drag_update), NULL);
#else
g_signal_connect(eb_for_pic, "motion_notify_event",
                   G_CALLBACK(viewer_motion), NULL);
#endif
g_signal_connect(eb_for_pic, "key_press_event",
                   G_CALLBACK(viewer_key_press), NULL);

/* need to ask for keypresses, and (for scaling) expose. */
gtk_widget_set_events(eb_for_pic,
#if !HAVE_DRAG_GESTURES
                      GDK_BUTTON1_MOTION_MASK|  /* to ignore drags */
#endif
                      GDK_KEY_PRESS_MASK|
                      GDK_EXPOSURE_MASK);

gtk_widget_show(image_widget);
gtk_widget_show(eb_for_pic);

/* alignment to centre the DA */
align=gtk_alignment_new(0.5,0.5,0.,0.);
gtk_container_add(GTK_CONTAINER(align),eb_for_pic);
gtk_widget_show(align);

/* scrolled window DA goes into (`inside' alignment) */
sw_for_pic=gtk_scrolled_window_new(NULL,NULL);
gtk_widget_set_can_focus(sw_for_pic, FALSE);
gtk_container_set_border_width(GTK_CONTAINER(sw_for_pic),0);
/* first `POLICY' is horiz, second is vert */
if(zoom)
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_pic),
                               (zoom_panorama&&zoom_panorama_sb)?GTK_POLICY_NEVER:GTK_POLICY_AUTOMATIC,
                               (zoom_panorama&&!zoom_panorama_sb)?GTK_POLICY_NEVER:GTK_POLICY_AUTOMATIC);
else
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_pic),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
gtk_paned_add2(GTK_PANED(pane),sw_for_pic);
gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sw_for_pic),
                                      align);
gtk_widget_show(sw_for_pic);


/* left-hand side */
vboxl=gtk_vbox_new(FALSE,0);
gtk_widget_set_can_focus(vboxl, FALSE);
gtk_paned_add1(GTK_PANED(pane),vboxl);
gtk_widget_show(vboxl);

/* event box for scrolled window for treeview (!), to make sure it has a
 * proper window to draw into (otherwise it screws up when pane-split pos
 * is near left of window). The image is ok on this count 'cos its
 * scrollbars are drawn to the right, i.e. off the window, and X clips
 * them. :-)
 *
 * This also serves a second purpose, as GTK 3's TreeView will, when keyboard
 * focus is acquired, automatically select an entry when none is selected.
 * (This does not happen in GTK 2, strangely enough.)  Since this is obviously
 * not something we want, and since there does not appear to be any way to
 * disable that behavior, we simply circumvent it by disabling focus for the
 * TreeView, enabling it instead for the event box, and having the latter
 * handle all key presses by itself.  It's silly, but at least it works.
 */
flist_sw_ebox=gtk_event_box_new();
gtk_box_pack_start(GTK_BOX(vboxl),flist_sw_ebox,TRUE,TRUE,0);

/* also capture key presses, so we can handle them in stead of treeview */
g_signal_connect(flist_sw_ebox, "key_press_event",
                   G_CALLBACK(selector_key_press), NULL);
gtk_widget_set_events(flist_sw_ebox,
                      GDK_KEY_PRESS_MASK);
gtk_widget_show(flist_sw_ebox);

/* now the scrolled window for treeview, and the treeview which goes into it. */
sw_for_flist=gtk_scrolled_window_new(NULL,NULL);
gtk_widget_set_can_focus(sw_for_flist, FALSE);
gtk_container_set_border_width(GTK_CONTAINER(sw_for_flist),0);

/* first `POLICY' is horiz, second is vert */
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_flist),
                               GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);

gtk_container_add(GTK_CONTAINER(flist_sw_ebox),sw_for_flist);
gtk_widget_show(sw_for_flist);

/* the liststore */
liststore = gtk_list_store_new(
    MODEL_NUM_COLUMNS,
    G_TYPE_STRING,     /* MODEL_NAME_COL */
    G_TYPE_POINTER,    /* MODEL_DATA_COL */
    G_TYPE_BOOLEAN,    /* MODEL_TAGGED_COL */
    GDK_TYPE_PIXBUF,   /* MODEL_TN_NORMAL_COL */
    GDK_TYPE_PIXBUF    /* MODEL_TN_SMALL_COL */
    );

/* the treeview */
treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(liststore));

/* column 1: thumbnail */
renderer = gtk_cell_renderer_pixbuf_new();
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview),
    -1,           /* position */
    "Thumbnail",  /* title */
    renderer,     /* cell */
    /* attributes */
    /* fetch pixbuf from thumbnail column */
    "pixbuf", (thin_rows ? MODEL_TN_SMALL_COL : MODEL_TN_NORMAL_COL),
    /* (make sure to re-add any additional attributes in toggle_thin_rows!) */
    NULL);
/* we'll need to remember this for toggle_thin_rows() */
thumbnail_renderer = renderer;

/* column 2: filename */
renderer = gtk_cell_renderer_text_new();
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview),
    -1,           /* position */
    "Filename",   /* title */
    renderer,     /* cell */
    /* attributes */
    "text",           MODEL_NAME_COL,    /* fetch text from name column */
    "foreground-set", MODEL_TAGGED_COL,  /* use foreground color if tagged */
    NULL);
/* set default properties for our filename cell renderer */
g_object_set(renderer,
    /* XXX colour used for tagging should be configurable */
    "foreground",     "red",  /* tagged color */
    "foreground-set", FALSE,  /* don't use foreground color by default */
    NULL);

/* don't display headers */
gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

/* select only one thing at a time */
selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

/* make sure treeview does not capture focus; let flist_sw_ebox do it */
gtk_widget_set_can_focus(treeview, FALSE);

/* refresh the focus row cursor when appropriate */
#if GTK_MAJOR_VERSION >= 3
/* GTK 3: whenever treeview redraws itself */
g_signal_connect_after(treeview, "draw",
    G_CALLBACK(draw_callback), NULL);
#else
/* GTK 2: set up a recurrent timer for this */
g_timeout_add(100, refresh_focus_row_timer_cb, GINT_TO_POINTER(FALSE));
/* (refreshing on expose-event isn't sufficient, but shouldn't hurt either) */
g_signal_connect_after(flist_sw_ebox, "expose-event",
    G_CALLBACK(refresh_focus_row), NULL);
#endif
/* also refresh the cursor on focus in/out */
g_signal_connect_after(flist_sw_ebox, "focus-in-event",
    G_CALLBACK(refresh_focus_row), NULL);
g_signal_connect_after(flist_sw_ebox, "focus-out-event",
    G_CALLBACK(refresh_focus_row), NULL);

/* selection callback - we save handler id as it needs to be blocked
 * in some circumstances.
 */
cb_selection_id = g_signal_connect(selection, "changed",
                                   G_CALLBACK(cb_selection), sw_for_pic);

set_thumbnail_column_width();		/* set width of thumbnail column */
gtk_tree_view_column_set_alignment(
    gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), VIEW_TN_COL),
    GTK_JUSTIFY_CENTER);

/* set up the sort comparison function */
gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(liststore),
    MODEL_NAME_COL,  /* sort_column_id */
    sort_cmp,        /* sort_func */
    NULL,            /* user_data */
    NULL);           /* destroy */
/* but make sure it's only called on request */
disable_sorting();

/* put in scrolled_window
 * (can't use ...add_with_viewport() if I want keyboard control to work)
 * (actually, the viewport() way seems so limited by comparison I'm
 * surprised the GTK+ tutorial describes it as `the' way to do it,
 * even if it does work for all widgets. :-/)
 */
gtk_container_add(GTK_CONTAINER(sw_for_flist),treeview);

/* menu stuff */
selector_menu = make_menu(ui_manager,
    "SelectorMenu",
    selector_menu_ui,
    selector_menu_entries, G_N_ELEMENTS(selector_menu_entries),
    selector_menu_toggle_entries, G_N_ELEMENTS(selector_menu_toggle_entries),
    selector_menu_sort_radio_entries, G_N_ELEMENTS(selector_menu_sort_radio_entries),
    G_CALLBACK(cb_set_order),
    selector_menu_datetime_radio_entries, G_N_ELEMENTS(selector_menu_datetime_radio_entries),
    G_CALLBACK(cb_set_timestamp_type)
    );

g_signal_connect(treeview, "button_press_event",
                   G_CALLBACK(selector_button_press), NULL);
g_signal_connect(treeview, "button_release_event",
                   G_CALLBACK(selector_button_release), NULL);
/* need to ask for button press (for menu), release (for tag), and key press */
gtk_widget_set_events(treeview,
                      GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
                      GDK_KEY_PRESS_MASK);

gtk_widget_show(treeview);

/* status line */
statusbar=gtk_statusbar_new();
gtk_box_pack_start(GTK_BOX(vboxl),statusbar,FALSE,FALSE,0);
/* get context ids */
sel_id=gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar),"selector");
tn_id= gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar),"thumbnails");
gtk_widget_show(statusbar);
if(!have_statusbar)
  gtk_widget_hide(statusbar);


/* fix menu options to reflect current status */
switch(filesel_sorttype)
  {
  case sort_name:	ptr="/SelectorMenu/DirectoryMenu/SortByName"; break;
  case sort_ext:	ptr="/SelectorMenu/DirectoryMenu/SortByExt"; break;
  case sort_size:	ptr="/SelectorMenu/DirectoryMenu/SortBySize"; break;
  default:
    /* sort_time */	ptr="/SelectorMenu/DirectoryMenu/SortByTime"; break;
  }
gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(gtk_ui_manager_get_widget(
    ui_manager,ptr)),TRUE);

switch(sort_timestamp_type)
  {
  default: ptr="/SelectorMenu/DirectoryMenu/DatetimeTypeMenu/"
             "DatetimeMtime"; break;
  case 1:  ptr="/SelectorMenu/DirectoryMenu/DatetimeTypeMenu/"
             "DatetimeCtime"; break;
  case 2:  ptr="/SelectorMenu/DirectoryMenu/DatetimeTypeMenu/"
             "DatetimeAtime"; break;
  }
gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(gtk_ui_manager_get_widget(
    ui_manager,ptr)),TRUE);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/SelectorMenu/sOptionsMenu/AutoHide")),auto_hide);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/SelectorMenu/sOptionsMenu/StatusBar")),have_statusbar);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/SelectorMenu/sOptionsMenu/ThumbnailMsgs")),tn_msgs);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/SelectorMenu/sOptionsMenu/ThinRows")),thin_rows);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/SelectorMenu/DirectoryMenu/ImagesOnly")),
  show_images_only);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/ReduceOnly")),
  zoom_reduce_only);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/Panorama")),
  zoom_panorama);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/Interpolate")),
  interp);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/MouseX")),
  mouse_scale_x);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/RevertOrient")),
  revert_orient);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/RevertScaling")),
  revert);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_ui_manager_get_widget(ui_manager,
                                "/ViewerMenu/vOptionsMenu/UseExif")),
  use_exif_orient);

zoom_widget=gtk_ui_manager_get_widget(ui_manager,
                                        "/ViewerMenu/vOptionsMenu/Zoom");
gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(zoom_widget),zoom);

/* disable thumbnail update and `thumbnail msgs' option if sel initially
 * hidden (loading from cmdline). Also disable go-to-dir/rescan; in theory
 * we *could* allow those, but it would be hairy. Another hairy one is
 * file rename, due to assumptions made about the file being in the current
 * dir. A few others are irrelevant or cause problems, too.
 */
if(hidden)
  {
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/UpdateTN"),FALSE);
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/UpdateTNRecursive"),FALSE);
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/FileMenu/Rename"),FALSE);
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/DirectoryMenu/ChangeDir"),FALSE);
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/DirectoryMenu/RescanDir"),FALSE);
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/sOptionsMenu/ThumbnailMsgs"),FALSE);
  gtk_action_set_sensitive(
    gtk_ui_manager_get_action(ui_manager,
                                "/SelectorMenu/sOptionsMenu/ThinRows"),FALSE);
  }

/* hook up an alternative quit key (q) */
gtk_widget_add_accelerator(
  gtk_ui_manager_get_widget(ui_manager,
                              "/SelectorMenu/Exit"),
  "activate",mainwin_accel_group,
  GDK_KEY_q,0,0);


/* severely hairy, but needed to allow menu to appear when a non-image
 * bit of the viewer window is selected.
 */
g_signal_connect(sw_for_pic,
                   "button_press_event",
                   G_CALLBACK(viewer_button_press), NULL);
g_signal_connect(sw_for_pic,
                   "button_release_event",
                   G_CALLBACK(viewer_button_release), NULL);
gtk_widget_set_events(sw_for_pic,
                      GDK_BUTTON_PRESS_MASK|
                      GDK_BUTTON_RELEASE_MASK);

/* have to carefully override this for scrollbars! */
g_signal_connect_after(
  gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(sw_for_pic)),
  "button_press_event", G_CALLBACK(viewer_sb_button_press), NULL);
g_signal_connect_after(
  gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(sw_for_pic)),
  "button_press_event", G_CALLBACK(viewer_sb_button_press), NULL);


g_signal_connect(mainwin, "configure_event",
                   G_CALLBACK(pic_win_resized), NULL);
/* ask for configure */
gtk_widget_set_events(mainwin,
                      GDK_STRUCTURE_MASK);


/* if hidden is set, we should hide it initially */
hide_saved_pos=default_sel_width;
gtk_paned_set_position(GTK_PANED(pane),hidden?1:hide_saved_pos);

gtk_widget_set_size_request(mainwin,100,50);

/* initially focus treeview's event box */
gtk_widget_set_can_focus(flist_sw_ebox, TRUE);
gtk_widget_grab_focus(flist_sw_ebox);

/* make sure option toggles are acknowledged */
listen_to_toggles=1;

gtk_widget_show(mainwin);

/*  now that the window is visible, we can finally determine its border size */
gtk_widget_get_allocation(sw_for_pic, &allocation);
gtk_widget_get_allocation(align, &allocation2);
sw_border_width=allocation.width-allocation2.width;
sw_border_height=allocation.height-allocation2.height;

/* set icon (XXX size should be configurable) */
icon = gdk_pixbuf_new_from_xpm_data((const char **) icon_48_xpm);
gtk_window_set_icon(GTK_WINDOW(mainwin), icon);

if(fullscreen)
  {
  GdkWindow *main_gdk_window = gtk_widget_get_window(mainwin);

  /* use mwm hints (I think) to turn off window frame */
  gdk_window_set_decorations(main_gdk_window,0);
  /* also, only allow window close to happen (not resize/move/mini/maximise) */
  gdk_window_set_functions(main_gdk_window,GDK_FUNC_CLOSE);
  }

/* adjust row heights */
fix_row_heights();

/* that's all folks */
}


void init_icon_pixbufs(void)
{
/* convert #included XPMs to pixbufs. We then use refs to these pixbufs
 * (increasing the ref count should avoid gtk_list_store_clear() freeing them).
 */
backend_create_pixbuf_from_xpm_data((const char **)dir_icon_xpm,
                                    &dir_icon);
backend_create_pixbuf_from_xpm_data((const char **)file_icon_xpm,
                                    &file_icon);

backend_create_pixbuf_from_xpm_data((const char **)dir_icon_small_xpm,
                         &dir_icon_small);
backend_create_pixbuf_from_xpm_data((const char **)file_icon_small_xpm,
                         &file_icon_small);
}


int isdir(char *filename)
{
struct stat sbuf;

if(stat(filename,&sbuf)==-1 || !S_ISDIR(sbuf.st_mode))
  return(0);

return(1);
}


void add_new_rows_from_cmdline(int argsleft,int argc,char *argv[])
{
int f;
struct stat sbuf;

numrows=0;
for(f=argc-argsleft;f<=argc-1;f++)
  {
  /* can't use isdir() as that has different reaction to stat() failing */
  if(stat(argv[f],&sbuf)!=-1 && !S_ISDIR(sbuf.st_mode))
    {
    if(add_new_row(argv[f],&sbuf))
      numrows++;
    }
  }

/* there may be no valid files; quit if so */
if(numrows==0)
  {
  fprintf(stderr,"xzgv: cannot open files given on command line!\n");
  exit(1);
  }
}


void echo_tagged_files(void)
{
struct row_data_tag *datptr;
char *ptr;
int f;

for(f=0;f<numrows;f++)
  {
  get_row_filename(f,&ptr);
  datptr=get_row_data(f);
  if(datptr && datptr->tagged)
    printf("%s\n",ptr);
  }
  g_free(ptr);
}


int main(int argc,char *argv[])
{
int f,argsleft;
int read_dir=1;

gtk_init(&argc,&argv);
backend_init();

find_xvpic_cols();

/* blank out past-positions array */
for(f=0;f<MAX_PASTPOS;f++)
  pastpos[f].dev=-1,pastpos[f].inode=-1;

get_config();				/* read config file if any */
argsleft=parse_options(argc,argv);	/* and command-line options */

if(hicol_dither>=0)
  fprintf(stderr,"Notice: The `dither-hicol' option is deprecated and has no effect.\n");

if(careful_jpeg>=0)
  fprintf(stderr,"Notice: The `careful-jpeg' option is deprecated and has no effect.\n");

if(image_bigness_threshold>=0)
  fprintf(stderr,"Notice: The `image-bigness-threshold' option is deprecated and has no effect.\n");

backend_set_interp(interp);

if(argsleft==1 && isdir(argv[optind]))
  xzgv_chdir(argv[optind]);	/* change to start directory */
else
  {
  if(argsleft>=1)
    {
    thin_rows=1;	/* use thin rows mode (good for filenames only :-)) */
    hidden=1;		/* hide selector (init_window() deals with this) */
    read_dir=0;		/* don't read dir on startup */
    cmdline_files=1;	/* needed for copymove.c to do the Right Thing */
    }
  }


/* now actually get going */
init_window();

init_icon_pixbufs();

/* read dir (unless loading pics from cmdline) */
if(read_dir)
  {
  add_new_rows_from_dir();
  set_focus_row(0);
  if(skip_parent && numrows>1)		/* skip .. if they asked us to */
    {
    char *ptr;
    
    /* check it's really `..' (won't be if in root dir) */
    get_row_filename(0,&ptr);
    if(strcmp(ptr,"..")==0)
      set_focus_row(1);
    g_free(ptr);
    }
  }
else
  {
  add_new_rows_from_cmdline(argsleft,argc,argv);
  gtk_tree_view_column_set_visible(
    gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), VIEW_TN_COL),
    FALSE);

  /* select first image, but make sure things are up and running first */
  do_gtk_stuff();
  set_focus_row(0);
  select_row(0);
  }

gtk_main();

if(show_tagged)
  echo_tagged_files();

exit(0);
}
