/* xzgv 0.8 - picture viewer for X, with file selector.
 * Copyright (C) 1999-2003 Russell Marks. See main.c for license details.
 *
 * rcfile.h - protos for rcfile.c and config vars.
 */

#define XZGV_VER	"0.8"

/* geometry bitmask for mainwin_flags */
#define GEOM_BITS_X_SET		1
#define GEOM_BITS_Y_SET		2
#define GEOM_BITS_W_SET		4
#define GEOM_BITS_H_SET		8

enum sorttypes
  {
  sort_name,sort_ext,sort_size,sort_time
  };

extern enum sorttypes filesel_sorttype;

extern int mainwin_x,mainwin_y;
extern int mainwin_w,mainwin_h;
extern int mainwin_flags;
extern int default_sel_width;

/* config vars */
extern int zoom;
extern int zoom_reduce_only;
extern int interp;
extern int have_statusbar;
extern int tn_msgs;
extern int thin_rows;
extern int auto_hide;
extern int revert;
extern int revert_orient;
extern int fullscreen;
extern int show_tagged;
extern int fast_recursive_update;
extern int hicol_dither;
extern int invert_logo;
extern int skip_parent;
extern int click_nextpic;
extern int mouse_scale_x;
extern double picgamma;
extern int image_bigness_threshold;
extern int delete_single_prompt;
extern int careful_jpeg;
extern int sort_timestamp_type;
extern int use_exif_orient;

extern void get_config(void);
extern int parse_options(int argc,char *argv[]);
