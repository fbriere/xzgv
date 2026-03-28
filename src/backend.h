/* xzgv - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks. See main.c for license details.
 *
 * backend.h
 */

struct _xzgv_image
  {
  unsigned char *rgb;		/* raw RGB data */
  int w,h;			/* width/height */
  void *backend_image;		/* backend's image type (opaque) */
  void *backend_ext;		/* any extra info needed by backend (opaque) */
  };

typedef struct _xzgv_image xzgv_image;


extern int backend_init(void);

extern void backend_flip_vert(xzgv_image *image);
extern void backend_flip_horiz(xzgv_image *image);
extern void backend_rotate_cw(xzgv_image *image);
extern void backend_rotate_cw(xzgv_image *image);
extern void backend_rotate_acw(xzgv_image *image);

extern xzgv_image *backend_create_image_from_file(char *filename);
extern int backend_get_orientation_from_file(char *filename);
extern int backend_render_pixmap_for_image(xzgv_image *image,int x,int y);
extern GdkPixmap *backend_get_and_detach_pixmap(xzgv_image *image);

extern void backend_pixmap_destroy(GdkPixmap *pixmap);
extern void backend_image_destroy(xzgv_image *image);

extern int backend_get_interp(void);
extern void backend_set_interp(int on);

extern int backend_create_pixmap_from_xpm_data(const char **data,
                                               GdkPixmap **pixmap,
                                               GdkBitmap **mask);
