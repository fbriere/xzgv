/* xzgv 0.6 - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks. See main.c for license details.
 *
 * main.h - header for main.c; there's plenty more I could list,
 *		but stuff here is on a `need to know' basis :-)
 */

/* which column is which in selector */
#define SELECTOR_TN_COL		0
#define SELECTOR_NAME_COL	1

struct clist_data_tag
  {
  char isdir;		/* 0=file, 1=dir. */
  char tagged;
  off_t size;
  time_t mtime,ctime,atime;
  int extofs;
  GdkPixmap *pm_norm;	/* normal thumbnail pixmap */
  GdkPixmap *pm_small;	/* small version for thin rows mode */
  GdkBitmap *pm_norm_mask;	/* mask, NULL for `real' thumbnails */
  GdkBitmap *pm_small_mask;	/* ...but used for dirs and no-tn files */
  };

extern GtkWidget *clist,*mainwin;
extern int numrows;
extern int cmdline_files;

extern void do_gtk_stuff(void);
extern xzgv_image *load_image(char *file,int for_thumbnail,
                                 int *origwp,int *orighp);
extern void make_visible_if_not(int row);
extern int get_tagged_state(int row);
extern int thumbnail_read_running(void);
extern void start_thumbnail_read(void);
extern void stop_thumbnail_read(void);
extern void blocking_thumbnail_read_visible(GtkWidget **checkptr);
extern void resort_finish(void);
extern GdkPixmap *xvpic2pixmap(unsigned char *xvpic,
                               int w,int h,GdkPixmap **smallp);
extern void new_pastpos(int row);
extern void error_dialog(char *title,char *msg);
extern void reinit_dir(int do_pastpos,int try_to_save_cursor_pos);
extern gint generic_win_destroy(GtkWidget *widget,int *destroyed_flag);
