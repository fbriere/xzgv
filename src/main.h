/* xzgv - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks. See main.c for license details.
 *
 * main.h - header for main.c; there's plenty more I could list,
 *		but stuff here is on a `need to know' basis :-)
 */

/* which column is which in the liststore model */
enum
{
  MODEL_NAME_COL = 0,  /* filename */
  MODEL_DATA_COL,      /* struct row_data_tag */
  MODEL_TAGGED_COL,    /* bool: row is tagged */

  MODEL_TN_NORMAL_COL, /* normal thumbnail */
  MODEL_TN_SMALL_COL,  /* small thumbnail */

  MODEL_NUM_COLUMNS
};
/* which column is which in the treeview */
enum
{
  VIEW_TN_COL = 0,  /* thumbnail */
  VIEW_NAME_COL,    /* filename */

  VIEW_NUM_COLUMNS
};

struct row_data_tag
  {
  char isdir;		/* 0=file, 1=dir. */
  char tagged;
  off_t size;
  time_t mtime,ctime,atime;
  int extofs;
  };

extern GtkWidget *treeview,*mainwin;
extern GtkAccelGroup *mainwin_accel_group;
extern int numrows;
extern int cmdline_files;
extern int focus_row;

extern void do_gtk_stuff(void);
extern xzgv_image *load_image(char *file,int for_thumbnail,
                                 int *origwp,int *orighp);
extern struct row_data_tag *get_row_data(int row);
extern void get_row_filename(int row, char **text);
extern void set_row_filename(int row, char *text);
extern int get_row_thumbnails(int row, GdkPixbuf **pixbuf, GdkPixbuf **small_pixbuf);
extern void set_row_thumbnails(int row, GdkPixbuf *pixbuf, GdkPixbuf *small_pixbuf);
extern void make_visible_if_not(int row);
extern int get_tagged_state(int row);
extern int thumbnail_read_running(void);
extern void start_thumbnail_read(void);
extern void stop_thumbnail_read(void);
extern void blocking_thumbnail_read_visible(GtkWidget **checkptr);
extern void resort_finish(void);
extern GdkPixbuf *xvpic2pixbuf(unsigned char *xvpic,
                               int w,int h,GdkPixbuf **smallp);
extern void new_pastpos(int row);
extern void error_dialog(char *title,char *msg);
extern void reinit_dir(int do_pastpos,int try_to_save_cursor_pos);
