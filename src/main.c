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
#include <gdk/gdkrgb.h>
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

/* for defence against render_pixmap recursive callbacks, etc.
 * Be sure to do RECURSE_PROTECT_END before *any* possible exit
 * (but as late as possible, of course).
 */
#define RECURSE_PROTECT_START	static int here=0; if(here) return; here=1
#define RECURSE_PROTECT_END	here=0


GtkWidget *drawing_area,*align,*sw_for_pic;
GtkWidget *clist,*statusbar,*sw_for_clist;
GtkWidget *selector_menu,*viewer_menu;
GtkWidget *zoom_widget;		/* widget for zoom opt on menu */
GtkWidget *pane;
guint sel_id;			/* selector id for statusbar messages */
guint tn_id;			/* `thumbnail' id for statusbar messages */

GtkWidget *mainwin;

guint8 xvpic_pal[256][3];		/* palette for thumbnails */

/* image & rendered pixmap for currently-loaded image */
xzgv_image *theimage=NULL;
GdkPixmap *thepixmap=NULL;

/* no-thumbnail icon pixmaps */
GdkPixmap *dir_icon,*file_icon;
GdkPixmap *dir_icon_small,*file_icon_small;
GdkBitmap *dir_icon_mask,*file_icon_mask;
GdkBitmap *dir_icon_small_mask,*file_icon_small_mask;

/* stuff for the idle-func thumbnail loading */
gint tn_idle_tag=-1;		/* tag returned by g_idle_add() */
float idle_xvpic_lastadjval;
int idle_xvpic_jumped=0;	/* true if xvpic load jumped ahead */
int idle_xvpic_blocked=0;	/* disables idle_xvpic_load() temporarily */
int idle_xvpic_called=0;	/* set when idle_xvpic_load is called */
int idle_xvpic_entry_idle;	/* entry placeholder when using g_idle_add() */

int numrows=0;			/* number of rows in clist */

gint zoom_resize_idle_tag=-1;	/* tag for zoom-resize kludge idle func */

int listen_to_toggles=0;	/* ignore fix-up toggles initially */
				/* (see init_window()) */
int in_nextprev=0;		/* needed to protect against recursion */

float orig_x,orig_y;		/* for image dragging with mouse */
int ignore_drag=1;		/* ignore image drags if true */
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
void render_pixmap(int reset_pos);
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

GtkItemFactory *make_menu(char *base,GtkItemFactoryEntry *menu_items,
                          int num_items)
{
GtkItemFactory *item_factory;

mainwin_accel_group=gtk_accel_group_new();

item_factory=gtk_item_factory_new(GTK_TYPE_MENU,base,mainwin_accel_group);

/* make menus */
gtk_item_factory_create_items(item_factory,num_items,menu_items,NULL);

/* add keys to window */
gtk_window_add_accel_group(GTK_WINDOW(mainwin),mainwin_accel_group);

return(item_factory);
}


gint cb_quit(GtkWidget *widget)
{
gtk_main_quit();

/* stop e.g. thumbnail update */
mainwin=NULL;

return(TRUE);
}


/* make a row visible if it's partly/fully obscured or `offscreen'. */
void make_visible_if_not(int row)
{
if(gtk_clist_row_is_visible(GTK_CLIST(clist),row)!=GTK_VISIBILITY_FULL)
  gtk_clist_moveto(GTK_CLIST(clist),row,0,0.5,0.);
}


/* moving the cursor while the clist is focused can screw up the display.
 * Instead, we unfocus the clist (if focused), change rows, then
 * (if it was previously focused) return focus to clist.
 */
void set_focus_row(int new_row)
{
int had_focus=gtk_widget_has_focus(clist);

if(had_focus)
  gtk_widget_grab_focus(drawing_area);

GTK_CLIST(clist)->focus_row=new_row;

if(had_focus)
  gtk_widget_grab_focus(clist);
}


/* gets whether a row is tagged or not.
 * Really just for convenience, and by analogy with set_tagged_state(). :-)
 */
int get_tagged_state(int row)
{
struct clist_data_tag *datptr;

if(row<0 || row>=numrows) return(0);

datptr=gtk_clist_get_row_data(GTK_CLIST(clist),row);
return(datptr->tagged);
}


/* sets whether a row is tagged or not.
 * tagged=0 to untag, 1 to tag, -1 to toggle.
 */
void set_tagged_state(int row,int tagged)
{
/* XXX colour used for tagging should be configurable */
static GdkColor col={0, 0xffff,0,0};	/* red */
struct clist_data_tag *datptr;

datptr=gtk_clist_get_row_data(GTK_CLIST(clist),row);
if(datptr->isdir) return;

if(datptr)
  {
  if(tagged==-1)
    datptr->tagged=!datptr->tagged;
  else
    datptr->tagged=tagged;
  }

gtk_clist_set_foreground(GTK_CLIST(clist),row,datptr->tagged?&col:NULL);
}


/* tag_file and untag_file are used when tagging from the keyboard,
 * or from the tag/untag file menu options.
 */
void cb_tag_file(void)
{
int row=GTK_CLIST(clist)->focus_row;

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
int row=GTK_CLIST(clist)->focus_row;

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

gtk_clist_freeze(GTK_CLIST(clist));

for(f=0;f<numrows;f++)
  set_tagged_state(f,1);

gtk_clist_thaw(GTK_CLIST(clist));
}


void cb_untag_all(void)
{
int f;

gtk_clist_freeze(GTK_CLIST(clist));

for(f=0;f<numrows;f++)
  set_tagged_state(f,0);

gtk_clist_thaw(GTK_CLIST(clist));
}


void cb_toggle_all(void)
{
int f;

gtk_clist_freeze(GTK_CLIST(clist));

for(f=0;f<numrows;f++)
  set_tagged_state(f,!get_tagged_state(f));

gtk_clist_thaw(GTK_CLIST(clist));
}


void cb_back_to_clist(void)
{
RECURSE_PROTECT_START;

/* unhide selector if it was hidden (whether auto-hidden or not) */
if(hidden)
  {
  gtk_paned_set_position(GTK_PANED(pane),hide_saved_pos);
  hidden=0;
  }

gtk_widget_set_can_focus(clist, TRUE);
gtk_widget_grab_focus(clist);

/* XXX kludge: make sure pic is fixed in zoom mode */
pic_win_resized(NULL,NULL);

RECURSE_PROTECT_END;
}


void cb_hide_selector(void)
{
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
  hide_saved_pos=sw_for_clist->allocation.width;
  gtk_paned_set_position(GTK_PANED(pane),1);
  hidden=1;
  }

/* XXX kludge: make sure pic is fixed in zoom mode */
pic_win_resized(NULL,NULL);

RECURSE_PROTECT_END;
}


void cb_iconify(void)
{
XIconifyWindow(GDK_WINDOW_XDISPLAY(mainwin->window),
               GDK_WINDOW_XID(mainwin->window),
               XScreenNumberOfScreen(XDefaultScreenOfDisplay(
                 GDK_WINDOW_XDISPLAY(mainwin->window))));
}


gint selector_button_press(GtkWidget *widget,GdkEventButton *event)
{
int row,col;

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
      /* stop the clist widget seeing it */
      g_signal_stop_emission_by_name(widget, "button_press_event");
      return(TRUE);	/* otherwise ignored, we do it on release */
      }
    break;
  
  case 3:
    /* move cursor to row clicked on (if any) */
    gtk_clist_get_selection_info(GTK_CLIST(clist),
                                 event->x,event->y,&row,&col);
    cb_back_to_clist();			/* show selector and switch to it */
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
int row,col;

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
      gtk_clist_get_selection_info(GTK_CLIST(clist),
                                   event->x,event->y,&row,&col);
      if(row>=0 && row<numrows)		/* sanity check :-) */
        set_tagged_state(row,-1);	/* toggle */
      return(TRUE);
      }
    break;
  }

return(FALSE);
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
    
    ignore_drag=0;
    next_on_release=1;
    /* set initial position */
    orig_x=event->x_root;
    orig_y=event->y_root;
    break;
  
  case 2:	/* middle button is a bit like Esc (handy in auto-hide mode) */
    if(hidden)
      cb_back_to_clist();	/* like Esc - show and focus */
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


gint clist_sw_ebox_button_press(GtkWidget *widget,GdkEventButton *event)
{
if(event->button==1)
  {
  /* this is a drag on the selector, so make sure image-dragging ignores it! */
  ignore_drag=1;
  next_on_release=0;	/* don't try to move to next image */
  }

return(FALSE);
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
  new_x=hadj->value+xadd;
  if(new_x<hadj->lower) new_x=hadj->lower;
  if(new_x>hadj->upper-hadj->page_size) new_x=hadj->upper-hadj->page_size;
  gtk_adjustment_set_value(hadj,new_x);
  }

if(yadd)
  {
  new_y=vadj->value+yadd;
  if(new_y<vadj->lower) new_y=vadj->lower;
  if(new_y>vadj->upper-vadj->page_size) new_y=vadj->upper-vadj->page_size;
  gtk_adjustment_set_value(vadj,new_y);
  }
}


gint viewer_motion(GtkWidget *widget,GdkEventMotion *event)
{
float diff_x,diff_y;

/* ignore it if the drag started on the selector */
if(ignore_drag) return(FALSE);

next_on_release=0;

/* ignore it if neither scrollbar is onscreen */
if(!GTK_SCROLLED_WINDOW(sw_for_pic)->hscrollbar_visible &&
   !GTK_SCROLLED_WINDOW(sw_for_pic)->vscrollbar_visible)
  return(TRUE);

/* XXX! should absorb all pending motion-notify events somehow, and
 * only use the X/Y pos of the last of those!
 */
/* have to use [xy]_root, as the window the events happen on will be moving! */
diff_x=orig_x-event->x_root;
diff_y=orig_y-event->y_root;
orig_x=event->x_root;
orig_y=event->y_root;

move_pic(diff_x,diff_y);

return(TRUE);
}


/* used by gtk_menu_popup() calls invoked from keyboard */
void keyboard_menu_pos(GtkMenu *menu,gint *xp,gint *yp,gboolean *push_in,GtkWidget *data)
{
gdk_window_get_position(mainwin->window,xp,yp);
*xp+=data->allocation.x;
*yp+=data->allocation.y;
}


/* this may call pic_win_resized, and can inherit the recursion problem
 * of render_pixmap, so be careful.
 */
int common_key_press(GdkEventKey *event)
{
int maxpos,oldpos,pos=sw_for_clist->allocation.width;
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
    maxpos=mainwin->allocation.width;
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
      move_pic(0.,-0.9*GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(sw_for_pic)))->page_size);
    else
      {
      RECURSE_PROTECT_END;
      return(FALSE);	/* don't stop event if not handled */
      }
    break;
  case GDK_KEY_Page_Down: case GDK_KEY_v:
  page_down:
    if(event->keyval!=GDK_KEY_v || (event->state&GDK_CONTROL_MASK))
      move_pic(0.,+0.9*GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(sw_for_pic)))->page_size);
    else
      {
      RECURSE_PROTECT_END;
      return(FALSE);
      }
    break;
  case GDK_KEY_minus:
  page_left:
    move_pic(-0.9*GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
      GTK_SCROLLED_WINDOW(sw_for_pic)))->page_size, 0.);
    break;
  case GDK_KEY_equal:
  page_right:
    move_pic(+0.9*GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
      GTK_SCROLLED_WINDOW(sw_for_pic)))->page_size, 0.);
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
    cb_back_to_clist();
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

/* one difference from normal clist keyboard-select behaviour;
 * we always select (rather than toggling), even if image was
 * previously selected.
 */
row=GTK_CLIST(clist)->focus_row;
if(row>=0 && row<numrows)
  {
  gtk_clist_unselect_all(GTK_CLIST(clist));
  /* this sets current_selection and zeroes in_nextprev too */
  gtk_clist_select_row(GTK_CLIST(clist),row,0);
  in_nextprev=0;
  }
else
  in_nextprev=0;
}


void cb_nextprev_tagged_image(int next,int view)
{
int f,dest,row=GTK_CLIST(clist)->focus_row;
struct clist_data_tag *datptr;
int incr=(next?1:-1);

if(in_nextprev) return;

dest=-1;
for(f=row+incr;(next && f<numrows) || (!next && f>=0);f+=incr)
  {
  datptr=gtk_clist_get_row_data(GTK_CLIST(clist),f);
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
    struct clist_data_tag *datptr;
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
      datptr=gtk_clist_get_row_data(GTK_CLIST(clist),f);
      if(!datptr->isdir)
        {
        gtk_clist_get_text(GTK_CLIST(clist),f,SELECTOR_NAME_COL,&ptr);
        nofiles=0;
        if(*ptr>=event->keyval)
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
      gtk_clist_moveto(GTK_CLIST(clist),GTK_CLIST(clist)->focus_row,0,0.5,0.);
    }
  }
else
  {
  int oldrow,incdec,up;
  int row=GTK_CLIST(clist)->focus_row;
  float vpage;
  
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
      if(row>0)
        {
        set_focus_row(row=row-1);
        if(gtk_clist_row_is_visible(GTK_CLIST(clist),row)!=GTK_VISIBILITY_FULL)
          gtk_clist_moveto(GTK_CLIST(clist),row,0,0.,0.);
        }
      break;
    case GDK_KEY_j:		/* down */
      if(row<numrows-1)
        {
        set_focus_row(row=row+1);
        if(gtk_clist_row_is_visible(GTK_CLIST(clist),row)!=GTK_VISIBILITY_FULL)
          gtk_clist_moveto(GTK_CLIST(clist),row,0,1.,0.);
        }
      break;

#define RET_IF_NOT_CONTROL	\
      if(!(event->state&GDK_CONTROL_MASK)) {RECURSE_PROTECT_END;return(FALSE);}
      
    case GDK_KEY_u: case GDK_KEY_v:	/* ctrl-u/v, like page up/down */
      RET_IF_NOT_CONTROL;
      up=(event->keyval==GDK_KEY_u);
      oldrow=row;
      vpage=GTK_ADJUSTMENT(
        gtk_clist_get_vadjustment(GTK_CLIST(clist)))->page_size;
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
        if(gtk_clist_row_is_visible(GTK_CLIST(clist),row)!=GTK_VISIBILITY_FULL)
          gtk_clist_moveto(GTK_CLIST(clist),row,0,up?0.:1.,0.);
        }
      break;
      
    case GDK_KEY_a:		/* ctrl-a, like ctrl-home */
      RET_IF_NOT_CONTROL;
      if(numrows)
        set_focus_row(0),make_visible_if_not(0);
      break;
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
                     (GtkMenuPositionFunc)keyboard_menu_pos,sw_for_clist,
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
  }

/* if we handled it, stop anything else getting the event. */
g_signal_stop_emission_by_name(widget, "key_press_event");

RECURSE_PROTECT_END;
return(TRUE);
}



void get_zoomed_size(int *swp,int *shp)
{
int scrnwide=sw_for_pic->allocation.width-sw_border_width;
int scrnhigh=sw_for_pic->allocation.height-sw_border_height;
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


/* render pixmap from image, resize drawing area to fit, and just
 * generally update things. Call this to update the image after pretty
 * much any change at all. :-)
 *
 * NB: this calls do_gtk_stuff(), so callers sensitive to recursion
 * beware! (In other words, defend against recursion with something like
 * the in_render stuff below, or RECURSE_PROTECT_START/END.)
 */
void render_pixmap(int reset_pos)
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
  backend_render_pixmap_for_image(theimage,sw,sh);

/* remove any backing pixmap */
gdk_window_set_back_pixmap(drawing_area->window,NULL,FALSE);

if(thepixmap)
  backend_pixmap_destroy(thepixmap),thepixmap=NULL;
if(!scaling_up_enabled)
  thepixmap=backend_get_and_detach_pixmap(theimage);

/* set drawing area to size of pixmap (also generates expose event) */
gtk_widget_set_size_request(align,sw,sh);
gtk_widget_set_size_request(drawing_area,sw,sh);

/* go back to top-left */
if(reset_pos)
  {
  gtk_adjustment_set_value(
    gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw_for_pic)),0.);
  gtk_adjustment_set_value(
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw_for_pic)),0.);
  }

/* let scrollbars appear/disappear as needed *before* we put pixmap on.
 * Without this you get a nasty `bounce' effect whenever scrollbars
 * appear/disappear. But it can still happen when resizing. (XXX)
 */
do_gtk_stuff();

/* put pixmap onto window as background. We could then free it, but
 * we don't - see idle_zoom_resize() for why. (Basically, we restore
 * the pic there after (to avoid `bounce') having removed it.)
 */
if(thepixmap)
  {
  gdk_window_set_back_pixmap(drawing_area->window,thepixmap,FALSE);
  gdk_window_clear(drawing_area->window);
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
  render_pixmap(1);	/* different size, render again */
else
  {
  gdk_window_set_back_pixmap(drawing_area->window,thepixmap,FALSE);
  gdk_window_clear(drawing_area->window);
  }
}


/* note that in zoom mode this inherits the recursion problem
 * of render_pixmap due to idle_zoom_resize, so be careful.
 */
gint pic_win_resized(GtkWidget *widget,GdkEventConfigure *event)
{
/* NB: this shouldn't use widget or event, as it's sometimes
 * called with them both being NULL (by the auto-hide stuff).
 */

/* Now also does this for non-zoomed pics in a (possibly vain)
 * attempt to reduce any `bounce' effect. But it should at least
 * save us getting bogged down when using `opaque resize' on
 * slower systems.
 */
gdk_window_set_back_pixmap(drawing_area->window,NULL,FALSE);

if(zoom_resize_idle_tag!=-1)
  g_source_remove(zoom_resize_idle_tag);

/* using resize priority gives better results if using `opaque resize',
 * but seems to break `normal' resizing. Not a good tradeoff. :-(
 */
zoom_resize_idle_tag=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE /*GTK_PRIORITY_RESIZE*/,
                                           (GSourceFunc)idle_zoom_resize,NULL,NULL);
return(FALSE);
}


void fix_row_heights(void)
{
gtk_clist_set_row_height(GTK_CLIST(clist),
                         thin_rows?ROW_HEIGHT_THIN:ROW_HEIGHT_NORMAL);
}


void set_thumbnail_column_width(void)
{
gtk_clist_set_column_width(GTK_CLIST(clist),SELECTOR_TN_COL,
                           thin_rows?(80/ROW_HEIGHT_DIV+1):80);
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
scrnwide=GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)))->page_size;
scrnhigh=GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)))->page_size;

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


/* call this before render_pixmap() */
void get_new_centre(int oldxsc,int oldysc,int newxsc,int newysc,
                    float *xp,float *yp)
{
GtkAdjustment *hadj,*vadj;
int oldx,oldy,x,y;

hadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));
vadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));

oldx=(int)hadj->value;
oldy=(int)vadj->value;
same_centre(&x,&y,newxsc,newysc,oldx,oldy,oldxsc,oldysc);

*xp=(float)x; *yp=(float)y;
}


/* call this after render_pixmap() */
void move_to_new_centre(float new_x,float new_y)
{
GtkAdjustment *hadj,*vadj;

hadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_hadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));
vadj=GTK_ADJUSTMENT(gtk_scrolled_window_get_vadjustment(
  GTK_SCROLLED_WINDOW(sw_for_pic)));

if(new_x<hadj->lower) new_x=hadj->lower;
if(new_x>hadj->upper-hadj->page_size) new_x=hadj->upper-hadj->page_size;
gtk_adjustment_set_value(hadj,new_x);

if(new_y<vadj->lower) new_y=vadj->lower;
if(new_y>vadj->upper-vadj->page_size) new_y=vadj->upper-vadj->page_size;
gtk_adjustment_set_value(vadj,new_y);
}


/* this inherits the same recursion problem as render_pixmap(),
 * so be careful.
 */
void scaling_finish(int oldxsc,int oldysc)
{
float x,y;

/* fairly hairy... :-/ */
if(oldxsc!=xscaling || oldysc!=yscaling)
  get_new_centre(oldxsc,oldysc,xscaling,yscaling,&x,&y);
render_pixmap(0);
gtk_widget_hide(drawing_area);
if(oldxsc!=xscaling || oldysc!=yscaling)
  move_to_new_centre(x,y);
gtk_widget_show(drawing_area);
/* making it smaller can look very nasty before it's redrawn,
 * but clearing the window looks fairly nasty too, so...
 */
if(xscaling<oldxsc || yscaling<oldysc)
  {
  gdk_window_clear(drawing_area->window);
  gdk_flush();
  }
gdk_flush();
}


/* turn off zoom (if enabled) without a call to render_pixmap() */
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
render_pixmap(1);

here=0;
}


/* Any callbacks which call render_pixmap() `must' avoid recursion,
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
render_pixmap(1);

listen_to_toggles=1;
}


void toggle_zoom_reduce(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

zoom_reduce_only=!zoom_reduce_only;
render_pixmap(1);

listen_to_toggles=1;
}


void toggle_zoom_panorama(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

zoom_panorama=!zoom_panorama;
render_pixmap(1);

listen_to_toggles=1;
}


void toggle_interp(gpointer cb_data,guint cb_action,GtkWidget *widget)
{
if(!listen_to_toggles || in_nextprev) return;
listen_to_toggles=0;

interp=!interp;

backend_set_interp(interp);

render_pixmap(0);

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
struct clist_data_tag *datptr;
GdkPixmap *pixmap;
GdkBitmap *mask;
int f;

if(!listen_to_toggles || in_nextprev) return;

listen_to_toggles=0;

gtk_clist_freeze(GTK_CLIST(clist));

thin_rows=!thin_rows;
fix_row_heights();
set_thumbnail_column_width();

/* switch pixmaps (normal for small, small for normal).
 * This is slightly tricky as there may be a thumbnail-read ongoing.
 * The current state is at least consistent though (it's not actually
 * multi-threaded or anything :-)), so just switch all which have
 * pixmaps.
 */
for(f=0;f<numrows;f++)
  {
  if(!gtk_clist_get_pixmap(GTK_CLIST(clist),f,SELECTOR_TN_COL,&pixmap,&mask))
    continue;
  
  datptr=gtk_clist_get_row_data(GTK_CLIST(clist),f);
  gtk_clist_set_pixmap(GTK_CLIST(clist),f,SELECTOR_TN_COL,
                       thin_rows?datptr->pm_small:datptr->pm_norm,
                       thin_rows?datptr->pm_small_mask:datptr->pm_norm_mask);
  }

gtk_clist_thaw(GTK_CLIST(clist));

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
gtk_clist_moveto(GTK_CLIST(clist),GTK_CLIST(clist)->focus_row,0,0.5,0.);

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
render_pixmap(1);
RECURSE_PROTECT_END;
}


void cb_mirror(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
backend_flip_horiz(theimage);
orient_current_state=orient_state_mirror[orient_current_state];
render_pixmap(1);
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
render_pixmap(1);
RECURSE_PROTECT_END;
}


void cb_rot_acw(void)
{
if (!theimage) return;
RECURSE_PROTECT_START;
backend_rotate_acw(theimage);
orient_current_state=orient_state_rot_acw[orient_current_state];
swap_xyscaling();
render_pixmap(1);
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
    render_pixmap(1);
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
idle_xvpic_lastadjval=gtk_clist_get_vadjustment(GTK_CLIST(clist))->value;
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
int row=-1,col=-1;

if(thumbnail_read_running()) return;

gtk_clist_get_selection_info(GTK_CLIST(clist),0,0,&row,&col);
if(row==-1) return;

idle_xvpic_lastadjval=gtk_clist_get_vadjustment(GTK_CLIST(clist))->value;
idle_xvpic_jumped=0;
entry=row;

while(entry!=-1 && mainwin && (!checkptr || *checkptr) &&
      gtk_clist_row_is_visible(GTK_CLIST(clist),entry)!=GTK_VISIBILITY_NONE)
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
struct clist_data_tag *datptr=NULL;

if(thumbnail_read_running())
  {
  stop_thumbnail_read();
  was_reading=1;
  }

/* set focus row to selection if it exists */
if(current_selection!=-1)
  set_focus_row(current_selection);

/* now we do everything in terms of the focus row.
 * get row data pointer (which is unique) so we can look the row up after.
 */
datptr=gtk_clist_get_row_data(GTK_CLIST(clist),GTK_CLIST(clist)->focus_row);

gtk_clist_sort(GTK_CLIST(clist));

/* look up data, and reselect it. */
if(datptr)
  {
  int tmp=gtk_clist_find_row_from_data(GTK_CLIST(clist),datptr);
  
  /* ..._find_row_from_data() returns -1 on error, great for this. :-) */
  if(current_selection!=-1)
    current_selection=tmp;
  
  /* not so good for this though. */
  set_focus_row((tmp!=-1)?tmp:0);
  
  /* Hmm. Ok, one thing we have to do which can't be done entirely
   * in terms of the focus row is to move the selection to the right place. :-)
   */
  if(current_selection!=-1)
    {
    /* block selection handler while selecting it, so we don't reload pic! */
    g_signal_handler_block(clist, cb_selection_id);
    gtk_clist_select_row(GTK_CLIST(clist),current_selection,0);
    g_signal_handler_unblock(clist, cb_selection_id);
    }
  }

/* now deal with visibility problems. This takes the same approach
 * as toggle_thin_rows() - if one selected move focus row to there
 * (did that before the sort), and either way force focus row to middle.
 */
gtk_clist_moveto(GTK_CLIST(clist),GTK_CLIST(clist)->focus_row,0,0.5,0.);

/* a thumbnail-read may have been ongoing; if so, restart it. */
if(was_reading)
  start_thumbnail_read();
}



void cb_name_order(void)
{
filesel_sorttype=sort_name;
resort_finish();
}

void cb_ext_order(void)
{
filesel_sorttype=sort_ext;
resort_finish();
}

void cb_size_order(void)
{
filesel_sorttype=sort_size;
resort_finish();
}

void cb_time_order(void)
{
filesel_sorttype=sort_time;
resort_finish();
}

void cb_mtime_type(void)
{
sort_timestamp_type=0; resort_finish();
}

void cb_ctime_type(void)
{
sort_timestamp_type=1; resort_finish();
}

void cb_atime_type(void)
{
sort_timestamp_type=2; resort_finish();
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
gtk_clist_select_row(GTK_CLIST(clist),row,0);	/* sets current_selection */
in_nextprev=0;
}


void cb_prev_image(void)
{
struct clist_data_tag *datptr;
int row;

if(in_nextprev) return;

if(current_selection==-1) return;	/* skip it if no current selection */

/* checking for previous image is slightly more complicated.
 * If current_selection is zero there's none, of course;
 * however, there's also none if the previous one is a dir.
 */
if(current_selection==0) return;

datptr=gtk_clist_get_row_data(GTK_CLIST(clist),current_selection-1);
if(datptr->isdir) return;

in_nextprev=1;

row=current_selection-1;
make_visible_if_not(row);

/* this causes a cb_selection call, which does the rest,
 * including zeroing in_nextprev.
 */
set_focus_row(row);
gtk_clist_select_row(GTK_CLIST(clist),row,0);	/* sets current_selection */
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


GdkPixmap *xvpic2pixmap(unsigned char *xvpic,int w,int h,GdkPixmap **smallp)
{
GdkPixmap *pixmap,*small_pixmap;
guint8 *buffer;
unsigned char *ptr=xvpic;
int x,y;
int small_w,small_h;

if(w==0 || h==0) return(NULL);


if (NULL == (pixmap=gdk_pixmap_new(mainwin->window,w,h, -1))) { 
    return(NULL);
}

small_w=w/ROW_HEIGHT_DIV;
small_h=h/ROW_HEIGHT_DIV;
if(small_w==0) small_w=1;
if(small_h==0) small_h=1;

if((small_pixmap=gdk_pixmap_new(mainwin->window,small_w,small_h,-1))==NULL)
{
    g_object_unref(pixmap);
    return(NULL);
}

buffer = malloc (w * h * sizeof (guint8) * 3);

if (NULL == buffer) {
    /* malloc failed */
    g_object_unref(pixmap);
    g_object_unref(small_pixmap);
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

gdk_draw_rgb_image(pixmap,clist->style->white_gc,0,0,w,h,
        GDK_RGB_DITHER_NORMAL,
        (guchar*)buffer, w * 3);
gdk_flush();

/* reuse image to draw scaled-down version for thin rows */


for(y=0;y<small_h;y++) {
  for(x=0;x<small_w;x++) {
      buffer[3*(y*small_w + x)+0] = xvpic_pal[xvpic[(y*w+x)*ROW_HEIGHT_DIV]][0];
      buffer[3*(y*small_w + x)+1] = xvpic_pal[xvpic[(y*w+x)*ROW_HEIGHT_DIV]][1];
      buffer[3*(y*small_w + x)+2] = xvpic_pal[xvpic[(y*w+x)*ROW_HEIGHT_DIV]][2];
  }
}

gdk_draw_rgb_image(small_pixmap,clist->style->white_gc,0,0,small_w, small_h,
        GDK_RGB_DITHER_NORMAL,
        (guchar*)buffer, small_w * 3);

*smallp=small_pixmap;

if (NULL != buffer) {
    free (buffer);
}

return(pixmap);
}


gint idle_xvpic_load(int *entryp)
{
static char buf[1024];
struct clist_data_tag *datptr;
char *ptr;
int f,w,h;
GdkPixmap *pixmap,*small_pixmap;
GdkBitmap *mask;
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

adjval=gtk_clist_get_vadjustment(GTK_CLIST(clist))->value;
if(adjval!=idle_xvpic_lastadjval)
  {
  int row=-1,col=-1;
  
  idle_xvpic_lastadjval=adjval;
  
  /* scrollbar position has changed, jump to first visible row.
   * (We'll make another pass to clean things up later.)
   * This can greatly reduce apparent thumbnail load time for
   * big dirs, even though in practice if you move about a lot
   * it can actually increase it somewhat. :-)
   */
  gtk_clist_get_selection_info(GTK_CLIST(clist),0,0,&row,&col);
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
  /* if there's already a pixmap there, skip it. */
  if(!gtk_clist_get_pixmap(GTK_CLIST(clist),*entryp,
                           SELECTOR_TN_COL,&pixmap,&mask))
    {
    /* construct filename for file's (possible) thumbnail */
    gtk_clist_get_text(GTK_CLIST(clist),*entryp,SELECTOR_NAME_COL,&ptr);
    strcpy(buf,".xvpics/");
    strncat(buf,ptr,sizeof(buf)-8-2);	/* above string is 8 chars long */
    
    datptr=gtk_clist_get_row_data(GTK_CLIST(clist),*entryp);
    
    /* if it's a dir, use ref to dir_icon pixmap. */
    if(datptr->isdir)
      {
      datptr->pm_norm=g_object_ref(dir_icon);
      datptr->pm_small=g_object_ref(dir_icon_small);
      datptr->pm_norm_mask=g_object_ref(dir_icon_mask);
      datptr->pm_small_mask=g_object_ref(dir_icon_small_mask);
      gtk_clist_set_pixmap(GTK_CLIST(clist),*entryp,SELECTOR_TN_COL,
                           thin_rows?datptr->pm_small:datptr->pm_norm,
                           (thin_rows?datptr->pm_small_mask:
                            datptr->pm_norm_mask));
      }
    else
      {
      /* it's a file, try to load a thumbnail for it */
      if(read_xvpic(buf,xvpic_data,&w,&h) &&
         (pixmap=xvpic2pixmap(xvpic_data,w,h,&small_pixmap))!=NULL)
        {
        datptr->pm_norm=pixmap;
        datptr->pm_small=small_pixmap;
        datptr->pm_norm_mask=datptr->pm_small_mask=NULL;
        gtk_clist_set_pixmap(GTK_CLIST(clist),*entryp,SELECTOR_TN_COL,
                             thin_rows?datptr->pm_small:datptr->pm_norm,
                             NULL);
        }
      else
        {
        /* no thumbnail then, use ref to file_icon pixmap. */
        datptr->pm_norm=g_object_ref(file_icon);
        datptr->pm_small=g_object_ref(file_icon_small);
        datptr->pm_norm_mask=g_object_ref(file_icon_mask);
        datptr->pm_small_mask=g_object_ref(file_icon_small_mask);
        gtk_clist_set_pixmap(GTK_CLIST(clist),*entryp,SELECTOR_TN_COL,
                             thin_rows?datptr->pm_small:datptr->pm_norm,
                             (thin_rows?datptr->pm_small_mask:
                              datptr->pm_norm_mask));
        }
      }
    }
  
  (*entryp)++;
  
  /* if we jumped, stop on first invisible row or end of list */
  if(idle_xvpic_jumped &&
     (*entryp>=numrows ||
      gtk_clist_row_is_visible(GTK_CLIST(clist),*entryp)==GTK_VISIBILITY_NONE))
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


/* remove everything from clist, freeing pixmaps beforehand */
void blast_clist(void)
{
int f;
struct clist_data_tag *datptr;

if(numrows==0) return;

gtk_clist_freeze(GTK_CLIST(clist));

/* stop any `currently'-running idle func to read thumbnails
 * (doing this now is probably overly paranoid, but it can't hurt)
 */
stop_thumbnail_read();

for(f=0;f<numrows;f++)
  {
  /* seems to free the pixmaps itself, but doesn't free the data AFAIK
   * (reasonable enough - the data could point to something static, etc.)
   * However, only one of our pixmaps (normal/small) is showing currently;
   * remove the other before removing the data.
   */
  datptr=gtk_clist_get_row_data(GTK_CLIST(clist),f);
  /* be careful - we may be halfway through thumbnail-read... */
  if(datptr)
    {
    if(datptr->pm_norm) g_object_unref(datptr->pm_norm);
    if(datptr->pm_norm_mask) g_object_unref(datptr->pm_norm_mask);
    if(datptr->pm_small) g_object_unref(datptr->pm_small);
    if(datptr->pm_small_mask) g_object_unref(datptr->pm_small_mask);
    free(datptr);
    }
  }

/* now remove all rows at once */
gtk_clist_clear(GTK_CLIST(clist));
numrows=0;

gtk_clist_thaw(GTK_CLIST(clist));
}


gint sort_cmp(GtkCList *clist,gconstpointer ptr1,gconstpointer ptr2)
{
GtkCListRow *row1=(GtkCListRow *)ptr1;
GtkCListRow *row2=(GtkCListRow *)ptr2;
char *txt1,*txt2;
struct clist_data_tag *dat1,*dat2;

txt1=GTK_CELL_TEXT(row1->cell[SELECTOR_NAME_COL])->text;
txt2=GTK_CELL_TEXT(row2->cell[SELECTOR_NAME_COL])->text;
dat1=row1->data;
dat2=row2->data;

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


int clist_add_new_row(char *filename,struct stat *sbuf)
{
struct clist_data_tag *datptr;
gchar *textarr[2];
char *ptr;
int row;
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
if((datptr=malloc(sizeof(struct clist_data_tag)))==NULL)
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
datptr->pm_norm=datptr->pm_small=NULL;	/* no pixmaps initially */
datptr->pm_norm_mask=datptr->pm_small_mask=NULL;

textarr[SELECTOR_TN_COL]="";
textarr[SELECTOR_NAME_COL]=filename;
row=gtk_clist_append(GTK_CLIST(clist),textarr);
gtk_clist_set_row_data(GTK_CLIST(clist),row,datptr);

/* we *could* put pixmaps in place for directories right now,
 * rather than waiting for idle_xvpic_load() to do it. However,
 * this a) seems to end up being a bit flickery despite the clist
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
void create_clist_from_dir(void)
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

gtk_clist_freeze(GTK_CLIST(clist));

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
  
  if(clist_add_new_row(dent->d_name,&sbuf))
    numrows++;
  }

closedir(dirfile);

if(numrows)
  {
  /* sort the list (using sort_cmp) */
  gtk_clist_sort(GTK_CLIST(clist));
  
  /* unselect the first row to give us a sane initial pos for
   * keyboard movement. (Doesn't seem to be necessary after sorting,
   * but can't hurt.)
   */
  gtk_clist_unselect_row(GTK_CLIST(clist),0,0);
  
  /* setup idle function to load thumbnails. */
  start_thumbnail_read();
  }

gtk_clist_thaw(GTK_CLIST(clist));
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
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(error_win)->vbox),
                   vbox,TRUE,TRUE,0);
gtk_widget_show(vbox);

gtk_container_set_border_width(GTK_CONTAINER(vbox),5);
gtk_container_set_border_width(
  GTK_CONTAINER(GTK_DIALOG(error_win)->action_area),2);

gtk_window_set_title(GTK_WINDOW(error_win),title);
gtk_window_set_resizable(GTK_WINDOW(error_win), TRUE);
gtk_window_set_position(GTK_WINDOW(error_win),GTK_WIN_POS_CENTER);
gtk_window_set_modal(GTK_WINDOW(error_win),TRUE);

label=gtk_label_new(msg);
gtk_box_pack_start(GTK_BOX(vbox),label,TRUE,TRUE,2);
gtk_widget_show(label);

/* add ok button */
action_tbl=gtk_table_new(1,3,TRUE);
gtk_box_pack_start(GTK_BOX(GTK_DIALOG(error_win)->action_area),
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
gtk_clist_unselect_all(GTK_CLIST(clist));
current_selection=-1;
cb_back_to_clist();		/* enable selector */

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
char *ptr,*tn;
int row;
int was_reading=0;

row=GTK_CLIST(clist)->focus_row;
gtk_clist_get_text(GTK_CLIST(clist),row,SELECTOR_NAME_COL,&ptr);

/* delete the file */
if(remove(ptr)==-1)
  {
  error_dialog("xzgv error","Unable to delete file!");
  return;
  }

cb_back_to_clist();

/* construct thumbnail filename early, as we're about to delete
 * the row containing the filename itself.
 */
tn=malloc(strlen(prefix)+strlen(ptr)+1);
if(tn)
  strcpy(tn,prefix),strcat(tn,ptr);

/* remove the row in the clist. We need to stop/restart thumbnail read
 * if it's running, as unexpectedly losing a row midway through could
 * cause problems.
 */
if(thumbnail_read_running())
  {
  stop_thumbnail_read();
  was_reading=1;
  }

gtk_clist_remove(GTK_CLIST(clist),row);
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

/* only now do we quit if we couldn't allocate mem for tn */
if(!tn) return;

remove(tn);		/* don't care if this fails */
rmdir(".xvpics");	/* same here */

free(tn);
}


void cb_delete_file(void)
{
static char *prefix="Really delete `",*suffix="'?";
struct clist_data_tag *datptr;
char *ptr,*msg;
int row;

row=GTK_CLIST(clist)->focus_row;
if(row<0 || row>=numrows) return;

gtk_clist_get_text(GTK_CLIST(clist),row,SELECTOR_NAME_COL,&ptr);
if(!ptr) return;

datptr=gtk_clist_get_row_data(GTK_CLIST(clist),row);
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
char *ptr,*oldname=NULL;

if(do_pastpos && try_to_save_cursor_pos)
  fprintf(stderr,"xzgv: both args to reinit_dir() set, bug alert :-)\n"),
    try_to_save_cursor_pos=0;

if(try_to_save_cursor_pos)
  {
  gtk_clist_get_text(GTK_CLIST(clist),GTK_CLIST(clist)->focus_row,
                     SELECTOR_NAME_COL,&ptr);
  if(!ptr || (oldname=malloc(strlen(ptr)+1))==NULL)
    try_to_save_cursor_pos=0;
  else
    strcpy(oldname,ptr);
  }

blast_clist();
create_clist_from_dir();
set_title(1);

if(try_to_save_cursor_pos)
  {
  int f;

  for(f=0;f<numrows;f++)
    {
    gtk_clist_get_text(GTK_CLIST(clist),f,SELECTOR_NAME_COL,&ptr);
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
    gtk_clist_moveto(GTK_CLIST(clist),row,0,0.5,0.);
    }
  }
}


void cb_reread_dir(void)
{
reinit_dir(0,1);		/* reread, don't do pastpos, save cursor pos */
}


void cb_copy_files(void)
{
cb_back_to_clist();
cb_copymove_file_or_tagged_files(0);
}


void cb_move_files(void)
{
cb_back_to_clist();
cb_copymove_file_or_tagged_files(1);
}


/* block keyboard/mouse input to selector */
void selector_block(void)
{
/* can't do this with g_signal_handler_block, as that doesn't block
 * the native clist handlers. Need to still have the handlers, but
 * have them actively ignore the events.
 */
ignore_selector_input=1;
}

/* and unblock it */
void selector_unblock(void)
{
ignore_selector_input=0;
}


void cb_selection(GtkWidget *clist,gint row,gint column,
                  GdkEventButton *event,GtkScrolledWindow *sw)
{
char *ptr;
xzgv_image *oldimage=theimage;
struct clist_data_tag *datptr;
int orient_lastpicexit_state=0;
int old_selection=current_selection;
FILE *test;
static int in_routine=0;

/* guard against recursion (from GTK+ updates) */
if(in_routine) return;

in_routine=1;

/* block mouse click/release and keys on selector while loading. */
selector_block();

current_selection=row;

gtk_clist_get_text(GTK_CLIST(clist),row,SELECTOR_NAME_COL,&ptr);

/* don't think this can happen, but what the heck */
if(!ptr)
  {
  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

/* this definitely can't be NULL; always allocated if the row exists */
datptr=gtk_clist_get_row_data(GTK_CLIST(clist),row);

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
    gtk_clist_unselect_all(GTK_CLIST(clist));
    }
  else	    /* a previous file was selected, reselect it (but don't reload) */
    {
    /* block selection handler while selecting it, so we don't reload pic! */
    g_signal_handler_block(clist, cb_selection_id);
    gtk_clist_select_row(GTK_CLIST(clist),current_selection,0);
    g_signal_handler_unblock(clist, cb_selection_id);
    }

  selector_unblock();
  in_nextprev=in_routine=0;
  return;
  }

if(datptr->isdir)
  {
  /* if it's a dir, chdir to it and read files there instead. */
  cb_back_to_clist();	/* in case of mouse click, to show pastpos action */
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
  cb_back_to_clist();	/* enable selector */
  
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
  render_pixmap(1);

/* only now can we safely free the old image (any old pixmap
 * will now no longer be onscreen).
 */
if(oldimage)
  backend_image_destroy(oldimage),oldimage=NULL;

gtk_statusbar_pop(GTK_STATUSBAR(statusbar),sel_id);

/* switch focus to pic */
gtk_widget_grab_focus(drawing_area);

/* stop us allowing kybd focus (until esc/tab) */
gtk_widget_set_can_focus(clist, FALSE);

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
 * b|clist of pics   |||                     |
 * o| in scrolled win|||                     |
 * x|1st col xvpic,  ||| pic in scrolled win |
 * l|2nd col fname.  |||                     |
 *  |                |v|                     |
 *  |------------------|                     |
 *  |status bar        |                     |
 *  `----------------------------------------'
 */
GtkWidget *vboxl;
GtkWidget *clist_sw_ebox;
GtkItemFactory *selector_menu_factory,*viewer_menu_factory;
GdkPixbuf *icon;
char *ptr;

/* selector right-button menu */
static GtkItemFactoryEntry selector_menu_items[]=
  {
  /* menu path		key		callback     cb args	item type */
  {"/_Update Thumbnails","u",		cb_update_tn,	0,	NULL},
  {"/_Recursive Update","<alt>u",	cb_update_tn_recursive,0,NULL},
  {"/sep1",		NULL,		NULL,		0,	"<Separator>"},
  {"/_File",		NULL,		NULL,		0,	"<Branch>"},
  {"/_File/_Open",	NULL,		view_focus_row_file, 0,	NULL},
  {"/_File/_Details...","colon",	cb_file_details,0,	NULL},
  {"/_File/Clo_se",	"<control>w",	cb_file_close,	0,	NULL},
  {"/_File/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_File/_Copy...",	"<shift>c",	cb_copy_files,	0,	NULL},
  {"/_File/_Move...",	"<shift>m",	cb_move_files,	0,	NULL},
  {"/_File/_Rename file...","<control>n",cb_rename_file,0,	NULL},
  {"/_File/De_lete file...","<control>d",cb_delete_file,0,	NULL},
  {"/_File/sep2",	NULL,		NULL,		0,	"<Separator>"},
  /* duplicate exit, as people will expect it here */
  {"/_File/E_xit",	NULL,		gtk_main_quit,	0,	NULL},
  {"/_Tagging",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Tagging/_Next Tagged","slash",	cb_selector_next_tagged,0,NULL},
  {"/_Tagging/_Previous Tagged","question",cb_selector_prev_tagged,0,NULL},
  {"/_Tagging/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Tagging/_Tag",	"equal",	cb_tag_file,	0,	NULL},
  {"/_Tagging/_Untag",	"minus",	cb_untag_file,	0,	NULL},
  {"/_Tagging/sep2",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Tagging/Tag _All","<alt>equal",	cb_tag_all,	0,	NULL},
  {"/_Tagging/U_ntag All","<alt>minus",	cb_untag_all,	0,	NULL},
  {"/_Tagging/T_oggle All","<alt>o",	cb_toggle_all,	0,	NULL},
  {"/_Directory",	NULL,		NULL,		0,	"<Branch>"},
  {"/_Directory/_Change...","<shift>g",	cb_goto_dir,	0,	NULL},
  {"/_Directory/_Rescan","<control>r",	cb_reread_dir,	0,	NULL},
  {"/_Directory/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Directory/_Images Only","<alt>i",	cb_show_images,	0,	"<ToggleItem>"},
  {"/_Directory/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Directory/Sort by _Name","<alt>n",cb_name_order,	0,	"<RadioItem>"},
  {"/_Directory/Sort by _Extension","<alt>e",cb_ext_order,
   0,"/Directory/Sort by Name"},
  {"/_Directory/Sort by _Size","<alt>s",cb_size_order,
   0,"/Directory/Sort by Name"},
  {"/_Directory/Sort by Time & _Date","<alt>d",cb_time_order,
   0,"/Directory/Sort by Name"},
  {"/_Directory/Time & Date _Type",NULL,NULL,		0,	"<Branch>"},
  {"/_Directory/Time & Date _Type/_Modification Time (mtime)",
   "<alt><shift>m",
   cb_mtime_type,0,"<RadioItem>"},
  {"/_Directory/Time & Date _Type/Attribute _Change Time (ctime)",
   "<alt><shift>c",
   cb_ctime_type,0,"/Directory/Time & Date Type/Modification Time (mtime)"},
  {"/_Directory/Time & Date _Type/_Access Time (atime)",
   "<alt><shift>a",
   cb_atime_type,0,"/Directory/Time & Date Type/Modification Time (mtime)"},
  {"/_Options",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Options/_Auto Hide", "<alt>a",	toggle_auto_hide,1,    "<ToggleItem>"},
  {"/_Options/_Status Bar", "<alt>b",	toggle_status,	1,     "<ToggleItem>"},
  {"/_Options/Thumb_nail Msgs",
   NULL,		toggle_tn_msgs,	1,     "<ToggleItem>"},
  {"/_Options/_Thin Rows", "v",		toggle_thin_rows, 1,   "<ToggleItem>"},
  {"/_Help",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Help/_Contents",  "F1",		cb_help_contents,0,	NULL},
  {"/_Help/The _File Selector",NULL,	cb_help_selector,0,	NULL},
  {"/_Help/_Index",	NULL,		cb_help_index,	0,	NULL},
  {"/_Help/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Help/_About...",	NULL,		cb_help_about,	0,	NULL},
  {"/sep2",		NULL,		NULL,		0,	"<Separator>"},
  {"/E_xit xzgv",	"<control>q",	gtk_main_quit,	0,	NULL}
  };

/* viewer right-button menu */
static GtkItemFactoryEntry viewer_menu_items[]=
  {
  /* menu path		key		callback     cb args	item type */
  {"/_Next Image",	"space",	cb_next_image,	0,	NULL},
  {"/_Previous Image",	"b",		cb_prev_image,	0,	NULL},
  {"/sep1",		NULL,		NULL,		0,	"<Separator>"},
  {"/_Tagging/_Tag then Next","<control>space",cb_tag_then_next,0,	NULL},
  {"/_Tagging/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Tagging/_Next Tagged","slash",cb_viewer_next_tagged,0,	NULL},
  {"/_Tagging/_Previous Tagged","question",cb_viewer_prev_tagged,0,NULL},
  {"/_Scaling",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Scaling/_Normal",	"n",		cb_normal,	0,	NULL},
  {"/_Scaling/_Double Scaling",	"d",		cb_scaling_double,0,	NULL},
  {"/_Scaling/_Halve Scaling",	"<shift>d",	cb_scaling_halve,0,	NULL},
  {"/_Scaling/_Add 1 to Scaling","s",		cb_scaling_add,	0,	NULL},
  {"/_Scaling/_Sub 1 from Scaling","<shift>s",	cb_scaling_sub,	0,	NULL},
  {"/_Scaling/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Scaling/_X Only/_Double Scaling",	"x",	cb_xscaling_double,0,	NULL},
  {"/_Scaling/_X Only/_Halve Scaling","<shift>x",cb_xscaling_halve,0,	NULL},
  {"/_Scaling/_X Only/_Add 1 to Scaling","<alt>x",cb_xscaling_add,0,	NULL},
  {"/_Scaling/_X Only/_Sub 1 from Scaling","<alt><shift>x",
   cb_xscaling_sub,0,	NULL},
  {"/_Scaling/_Y Only/_Double Scaling",	"y",	cb_yscaling_double,0,	NULL},
  {"/_Scaling/_Y Only/_Halve Scaling","<shift>y",cb_yscaling_halve,0,	NULL},
  {"/_Scaling/_Y Only/_Add 1 to Scaling","<alt>y",cb_yscaling_add,0,	NULL},
  {"/_Scaling/_Y Only/_Sub 1 from Scaling","<alt><shift>y",
   cb_yscaling_sub,0,	NULL},
  {"/O_rientation",	NULL,		NULL,		0,	"<Branch>"},
  {"/O_rientation/_Normal","<shift>n",	cb_normal_orient,0,	NULL},
  {"/O_rientation/_Mirror (horiz)","m",	cb_mirror,	0,	NULL},
  {"/O_rientation/_Flip (vert)","f",	cb_flip,	0,	NULL},
  {"/O_rientation/_Rotate Right","r",	cb_rot_cw,	0,	NULL},
  {"/O_rientation/Rotate _Left","<shift>r",cb_rot_acw,	0,	NULL},
  {"/_Window",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Window/_Hide Selector","<shift>z",cb_hide_selector,0,	NULL},
  /* I would normally write `minimise', but it looks a bit odd and the -ize
   * spelling is entrenched, so I'll live with it. :-) */
  {"/_Window/_Minimize",	"<control>z",	cb_iconify,	0,	NULL},
  {"/_Options",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Options/_Zoom (fit to window)","z",toggle_zoom,	1,     "<ToggleItem>"},
  {"/_Options/When Zooming _Reduce Only","<alt>r",
   toggle_zoom_reduce,1,  "<ToggleItem>"},
  {"/_Options/When Zooming _Panorama","<alt>p",
   toggle_zoom_panorama,1,  "<ToggleItem>"},
  {"/_Options/_Interpolate when Scaling","i",toggle_interp,1,  "<ToggleItem>"},
  {"/_Options/_Ctl+Click Scales X Axis","<alt>c",
   toggle_mouse_x,	1,	"<ToggleItem>"},
  {"/_Options/Use _Exif Orientation",NULL,toggle_exif_orient,1, "<ToggleItem>"},
  {"/_Options/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Options/Revert _Scaling For New Pic",NULL,
   toggle_revert,	1,	"<ToggleItem>"},
  {"/_Options/Revert _Orient. For New Pic",NULL,
   toggle_revert_orient,1,	"<ToggleItem>"},
  {"/_Help",		NULL,		NULL,		0,	"<Branch>"},
  {"/_Help/_Contents",  "F1",		cb_help_contents,0,	NULL},
  {"/_Help/The _Viewer",NULL,		cb_help_viewer,	0,	NULL},
  {"/_Help/_Index",	NULL,		cb_help_index,	0,	NULL},
  {"/_Help/sep1",	NULL,		NULL,		0,	"<Separator>"},
  {"/_Help/_About...",	NULL,		cb_help_about,	0,	NULL},
  {"/sep2",		NULL,		NULL,		0,	"<Separator>"},
  {"/E_xit to Selector","Escape",	cb_back_to_clist, 0,	NULL}
  };


mainwin=gtk_window_new(GTK_WINDOW_TOPLEVEL);
gtk_widget_set_can_focus(mainwin, TRUE);
g_signal_connect(mainwin, "destroy",
                   G_CALLBACK(cb_quit), NULL);
/* don't include dir if selector initially hidden (loading from cmdline) */
set_title(!hidden);

set_window_pos_and_size();


pane=gtk_hpaned_new();
gtk_widget_set_can_focus(pane, TRUE);
gtk_container_add(GTK_CONTAINER(mainwin),pane);
gtk_widget_show(pane);


/* right-hand side */

/* the drawing area used for the pic */
drawing_area=gtk_drawing_area_new();
gtk_widget_set_can_focus(drawing_area, TRUE);
viewer_menu_factory=make_menu("<main>",viewer_menu_items,
                              sizeof(viewer_menu_items)/sizeof(
                                viewer_menu_items[0]));
viewer_menu=gtk_item_factory_get_widget(viewer_menu_factory,"<main>");

g_signal_connect(drawing_area, "motion_notify_event",
                   G_CALLBACK(viewer_motion), NULL);
g_signal_connect(drawing_area, "key_press_event",
                   G_CALLBACK(viewer_key_press), NULL);

/* need to ask for motion while button 1 is pressed (for drag),
 * keypresses, and (for scaling) expose.
 */
gtk_widget_set_events(drawing_area,
                      GDK_BUTTON1_MOTION_MASK|GDK_KEY_PRESS_MASK|
                      GDK_EXPOSURE_MASK);

gtk_widget_show(drawing_area);

/* alignment to centre the DA */
align=gtk_alignment_new(0.5,0.5,0.,0.);
gtk_container_add(GTK_CONTAINER(align),drawing_area);
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

/* event box for scrolled window for clist (!), to make sure it has a
 * proper window to draw into (otherwise it screws up when pane-split pos
 * is near left of window). The image is ok on this count 'cos its
 * scrollbars are drawn to the right, i.e. off the window, and X clips
 * them. :-)
 */
clist_sw_ebox=gtk_event_box_new();
gtk_box_pack_start(GTK_BOX(vboxl),clist_sw_ebox,TRUE,TRUE,0);

/* pass on left-button motion events to viewer's image-dragging stuff,
 * so it doesn't stop dragging just because you drag the pointer over
 * the selector. This means we have to carefully ignore any drags which
 * start in the selector though, hence the left-button-press event
 * handling here.
 */
g_signal_connect(clist_sw_ebox, "button_press_event",
                   G_CALLBACK(clist_sw_ebox_button_press), NULL);
g_signal_connect(clist_sw_ebox, "motion_notify_event",
                   G_CALLBACK(viewer_motion), NULL);
gtk_widget_set_events(clist_sw_ebox,
                      GDK_BUTTON_PRESS_MASK|GDK_BUTTON1_MOTION_MASK);
gtk_widget_show(clist_sw_ebox);

/* now the scrolled window for clist, and the clist which goes into it. */
sw_for_clist=gtk_scrolled_window_new(NULL,NULL);
gtk_widget_set_can_focus(sw_for_clist, FALSE);
gtk_container_set_border_width(GTK_CONTAINER(sw_for_clist),0);

/* first `POLICY' is horiz, second is vert */
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_for_clist),
                               GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);

gtk_container_add(GTK_CONTAINER(clist_sw_ebox),sw_for_clist);
gtk_widget_show(sw_for_clist);

/* the clist */
clist=gtk_clist_new(2);
/* select only one thing at a time */
gtk_clist_set_selection_mode(GTK_CLIST(clist),GTK_SELECTION_SINGLE);

/* selection callback - we save handler id as it needs to be blocked
 * in some circumstances.
 */
cb_selection_id = g_signal_connect(clist, "select_row",
                                   G_CALLBACK(cb_selection), sw_for_pic);

set_thumbnail_column_width();		/* set width of thumbnail column */
gtk_clist_set_column_auto_resize(GTK_CLIST(clist),SELECTOR_NAME_COL,TRUE);
/* set heights to thin initially; see end of routine for why */
gtk_clist_set_row_height(GTK_CLIST(clist),ROW_HEIGHT_THIN);
gtk_clist_set_column_justification(GTK_CLIST(clist),
                                   SELECTOR_TN_COL,GTK_JUSTIFY_CENTER);
gtk_clist_set_compare_func(GTK_CLIST(clist),sort_cmp);
gtk_clist_set_sort_column(GTK_CLIST(clist),SELECTOR_NAME_COL);

/* put in scrolled_window
 * (can't use ...add_with_viewport() if I want keyboard control to work)
 * (actually, the viewport() way seems so limited by comparison I'm
 * surprised the GTK+ tutorial describes it as `the' way to do it,
 * even if it does work for all widgets. :-/)
 */
gtk_container_add(GTK_CONTAINER(sw_for_clist),clist);

/* menu stuff */
selector_menu_factory=make_menu("<main>",selector_menu_items,
                                sizeof(selector_menu_items)/sizeof(
                                  selector_menu_items[0]));
selector_menu=gtk_item_factory_get_widget(selector_menu_factory,"<main>");

g_signal_connect(clist, "button_press_event",
                   G_CALLBACK(selector_button_press), NULL);
g_signal_connect(clist, "button_release_event",
                   G_CALLBACK(selector_button_release), NULL);
g_signal_connect(clist, "key_press_event",
                   G_CALLBACK(selector_key_press), NULL);
/* need to ask for button press (for menu), release (for tag), and key press */
gtk_widget_set_events(clist,
                      GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
                      GDK_KEY_PRESS_MASK);

gtk_widget_show(clist);

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
  case sort_name:	ptr="<main>/Directory/Sort by Name"; break;
  case sort_ext:	ptr="<main>/Directory/Sort by Extension"; break;
  case sort_size:	ptr="<main>/Directory/Sort by Size"; break;
  default:
    /* sort_time */	ptr="<main>/Directory/Sort by Time & Date"; break;
  }
gtk_check_menu_item_set_active(
  &(GTK_RADIO_MENU_ITEM(gtk_item_factory_get_widget(
    selector_menu_factory,ptr))->check_menu_item),TRUE);

switch(sort_timestamp_type)
  {
  default: ptr="<main>/Directory/Time & Date Type/"
             "Modification Time (mtime)"; break;
  case 1:  ptr="<main>/Directory/Time & Date Type/"
             "Attribute Change Time (ctime)"; break;
  case 2:  ptr="<main>/Directory/Time & Date Type/"
             "Access Time (atime)"; break;
  }
gtk_check_menu_item_set_active(
  &(GTK_RADIO_MENU_ITEM(gtk_item_factory_get_widget(
    selector_menu_factory,ptr))->check_menu_item),TRUE);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Options/Auto Hide")),auto_hide);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Options/Status Bar")),have_statusbar);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Options/Thumbnail Msgs")),tn_msgs);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Options/Thin Rows")),thin_rows);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Directory/Images Only")),
  show_images_only);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/When Zooming Reduce Only")),
  zoom_reduce_only);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/When Zooming Panorama")),
  zoom_panorama);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/Interpolate when Scaling")),
  interp);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/Ctl+Click Scales X Axis")),
  mouse_scale_x);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/Revert Orient. For New Pic")),
  revert_orient);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/Revert Scaling For New Pic")),
  revert);

gtk_check_menu_item_set_active(
  GTK_CHECK_MENU_ITEM(
    gtk_item_factory_get_widget(viewer_menu_factory,
                                "<main>/Options/Use Exif Orientation")),
  use_exif_orient);

zoom_widget=gtk_item_factory_get_widget(viewer_menu_factory,
                                        "<main>/Options/Zoom (fit to window)");
gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(zoom_widget),zoom);

/* disable thumbnail update and `thumbnail msgs' option if sel initially
 * hidden (loading from cmdline). Also disable go-to-dir/rescan; in theory
 * we *could* allow those, but it would be hairy. Another hairy one is
 * file rename, due to assumptions made about the file being in the current
 * dir. A few others are irrelevant or cause problems, too.
 */
if(hidden)
  {
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Update Thumbnails"),FALSE);
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Recursive Update"),FALSE);
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/File/Rename file..."),FALSE);
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Directory/Change..."),FALSE);
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Directory/Rescan"),FALSE);
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Options/Thumbnail Msgs"),FALSE);
  gtk_widget_set_sensitive(
    gtk_item_factory_get_widget(selector_menu_factory,
                                "<main>/Options/Thin Rows"),FALSE);
  }

/* hook up an alternative quit key (q) */
gtk_widget_add_accelerator(
  gtk_item_factory_get_widget(selector_menu_factory,
                              "<main>/Exit xzgv"),
  "activate",mainwin_accel_group,
  GDK_KEY_q,0,0);


/* severely hairy, but needed to allow menu to appear when a non-image
 * bit of the viewer window is selected. Also allows drags in non-image
 * bits, which is handy for really thin images.
 */
g_signal_connect(sw_for_pic,
                   "button_press_event",
                   G_CALLBACK(viewer_button_press), NULL);

/* have to carefully override this for scrollbars! */
g_signal_connect_after(
  GTK_SCROLLED_WINDOW(sw_for_pic)->hscrollbar,
  "button_press_event", G_CALLBACK(viewer_sb_button_press), NULL);
g_signal_connect_after(
  GTK_SCROLLED_WINDOW(sw_for_pic)->vscrollbar,
  "button_press_event", G_CALLBACK(viewer_sb_button_press), NULL);


g_signal_connect(mainwin, "configure_event",
                   G_CALLBACK(pic_win_resized), NULL);
/* this catches dragging across the pane splitter */
g_signal_connect(mainwin, "motion_notify_event",
                   G_CALLBACK(viewer_motion), NULL);
g_signal_connect(mainwin,
                   "button_release_event",
                   G_CALLBACK(viewer_button_release), NULL);
/* ask for configure and left-button drag */
gtk_widget_set_events(mainwin,
                      GDK_STRUCTURE_MASK|GDK_BUTTON1_MOTION_MASK|
                      GDK_BUTTON_RELEASE_MASK);


/* if hidden is set, we should hide it initially */
hide_saved_pos=default_sel_width;
gtk_paned_set_position(GTK_PANED(pane),hidden?1:hide_saved_pos);

gtk_widget_set_size_request(mainwin,100,50);

/* initially focus clist */
gtk_widget_grab_focus(clist);

/* make sure option toggles are acknowledged */
listen_to_toggles=1;

gtk_widget_show(mainwin);

/*  now that the window is visible, we can finally determine its border size */
sw_border_width=sw_for_pic->allocation.width-align->allocation.width;
sw_border_height=sw_for_pic->allocation.height-align->allocation.height;

/* set icon (XXX size should be configurable) */
icon = gdk_pixbuf_new_from_xpm_data((const char **) icon_48_xpm);
gtk_window_set_icon(GTK_WINDOW(mainwin), icon);

if(fullscreen)
  {
  /* use mwm hints (I think) to turn off window frame */
  gdk_window_set_decorations(mainwin->window,0);
  /* also, only allow window close to happen (not resize/move/mini/maximise) */
  gdk_window_set_functions(mainwin->window,GDK_FUNC_CLOSE);
  }

/* adjust row heights now, which should leave filename text
 * auto-adjusted to be roughly centred. This is really kludgey, but
 * I couldn't get this to work using vertical shifts for both
 * thin_rows modes.
 */
gtk_clist_set_row_height(GTK_CLIST(clist),ROW_HEIGHT_NORMAL);

/* that's all folks */
}


void init_icon_pixmaps(void)
{
/* convert #included XPMs to pixmaps. We then use refs to these pixmaps
 * (increasing the ref count should avoid gtk_clist_clear() freeing them).
 */
backend_create_pixmap_from_xpm_data((const char **)dir_icon_xpm,
                                    &dir_icon,&dir_icon_mask);
backend_create_pixmap_from_xpm_data((const char **)file_icon_xpm,
                                    &file_icon,&file_icon_mask);

backend_create_pixmap_from_xpm_data((const char **)dir_icon_small_xpm,
                         &dir_icon_small,&dir_icon_small_mask);
backend_create_pixmap_from_xpm_data((const char **)file_icon_small_xpm,
                         &file_icon_small,&file_icon_small_mask);
}


int isdir(char *filename)
{
struct stat sbuf;

if(stat(filename,&sbuf)==-1 || !S_ISDIR(sbuf.st_mode))
  return(0);

return(1);
}


void create_clist_from_cmdline(int argsleft,int argc,char *argv[])
{
int f;
struct stat sbuf;

numrows=0;
for(f=argc-argsleft;f<=argc-1;f++)
  {
  /* can't use isdir() as that has different reaction to stat() failing */
  if(stat(argv[f],&sbuf)!=-1 && !S_ISDIR(sbuf.st_mode))
    {
    if(clist_add_new_row(argv[f],&sbuf))
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
struct clist_data_tag *datptr;
char *ptr;
int f;

for(f=0;f<numrows;f++)
  {
  gtk_clist_get_text(GTK_CLIST(clist),f,SELECTOR_NAME_COL,&ptr);
  datptr=gtk_clist_get_row_data(GTK_CLIST(clist),f);
  if(datptr && datptr->tagged)
    printf("%s\n",ptr);
  }
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

init_icon_pixmaps();

/* read dir (unless loading pics from cmdline) */
if(read_dir)
  {
  create_clist_from_dir();
  if(skip_parent && numrows>1)		/* skip .. if they asked us to */
    {
    char *ptr;
    
    /* check it's really `..' (won't be if in root dir) */
    gtk_clist_get_text(GTK_CLIST(clist),0,SELECTOR_NAME_COL,&ptr);
    if(strcmp(ptr,"..")==0)
      set_focus_row(1);
    }
  }
else
  {
  create_clist_from_cmdline(argsleft,argc,argv);
  gtk_clist_set_column_width(GTK_CLIST(clist),SELECTOR_TN_COL,1);
  /* select first image, but make sure things are up and running first */
  do_gtk_stuff();
  gtk_clist_select_row(GTK_CLIST(clist),0,0);
  }

/* initialise thin_rows stuff (has to be after above so there's something
 * (or nothing :-)) in the clist). Only needed if thin_rows is initially
 * true, though.
 */
if(thin_rows)
  fix_row_heights();

gtk_main();

if(show_tagged)
  echo_tagged_files();

exit(0);
}
