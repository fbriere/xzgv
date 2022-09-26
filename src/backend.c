/* xzgv - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks.
 * Copyright (C) 2007 Reuben Thomas.
 * See main.c for license details.
 * 
 * backend.c - picture rendering and (to a certain extent) loading.
 *
 * This is intended to be a reasonably generic wrapper for the library
 * which is actually doing the work, to ease any transition, or allow
 * extra backends.
 *
 * The basic assumptions are:
 *
 * - all pictures loaded as 24-bit.
 *
 * - a 24-bit copy is stored in an opaque (or mostly opaque) image
 *   structure of some sort; this is then rendered as needed.
 *
 * - there can be at least one pixmap `associated' with the image;
 *   that is, you can say `render', and it does that saving the pixmap
 *   details somewhere, so you can just say `draw' later. This could
 *   be emulated via xzgv_image's `backend_ext' pointer if need be.
 *
 * - it's possible to get a closest-match colour.
 *
 * - XXX probably others :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <libexif/exif-data.h>

#include "backend.h"




/* *******************************************************************
 * *******************************************************************
 * ************                                           ************
 * ************           gdk-pixbuf backend              ************
 * ************                                           ************
 * *******************************************************************
 * *******************************************************************
 */
#include <gdk-pixbuf/gdk-pixbuf.h>

/* get backend image, casted to appropriate type */
#define BACKEND_IMAGE(x)	((x)->backend_image)

/* Dithering type */
GdkRgbDither dither_type;

/* Interpolation type */
GdkInterpType interp_type;

/* do any initialisation the backend needs. Should include any
 * visual/colormap change required.
 * returns 1 if ok, else 0. (If 0, should output descriptive error msg.)
 */
int backend_init(void)
{
gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
gtk_widget_set_default_visual(gdk_rgb_get_visual());

dither_type = GDK_RGB_DITHER_NORMAL;

interp_type = GDK_INTERP_NEAREST;

return(1);
}


/* init an image - the usual thing is to clear out the struct.
 * You don't need to do anything here if it's not required by the
 * backend; it's primarily to set xzgv_image correctly.
 * Note that stuff done here should be stuff which NEVER fails.
 */
void backend_image_init(xzgv_image *image)
{
image->rgb=NULL;
image->w=0; image->h=0;
image->backend_image=NULL;
image->backend_ext=NULL;
}


/* convenience function to update `public' info in xzgv_image
 * from private info. You don't have to have this, but you *do*
 * have to keep those fields up-to-date somehow.
 */
static void public_info_update(xzgv_image *image)
{
image->rgb=gdk_pixbuf_get_pixels(BACKEND_IMAGE(image));
image->w=gdk_pixbuf_get_width(BACKEND_IMAGE(image));
image->h=gdk_pixbuf_get_height(BACKEND_IMAGE(image));
}


/* mark an image as `changed', i.e. `dirty' it. */
void backend_image_changed(xzgv_image *image)
{
/* XXX */
}


/* flip image vertically. Should `dirty' image if needed.
 * Should also update xzgv_image's rgb/w/h fields (use public_info_update()).
 */
void backend_flip_vert(xzgv_image *image)
{
GdkPixbuf *new_img = gdk_pixbuf_flip(BACKEND_IMAGE(image), FALSE);
g_object_unref(BACKEND_IMAGE(image));
BACKEND_IMAGE(image) = new_img;
public_info_update(image);
}


/* flip image horizontally, similarly. */
void backend_flip_horiz(xzgv_image *image)
{
GdkPixbuf *new_img = gdk_pixbuf_flip(BACKEND_IMAGE(image), TRUE);
g_object_unref(BACKEND_IMAGE(image));
BACKEND_IMAGE(image) = new_img;
public_info_update(image);
}


/* rotate image clockwise, similarly. */
void backend_rotate_cw(xzgv_image *image)
{
GdkPixbuf *new_img = gdk_pixbuf_rotate_simple(BACKEND_IMAGE(image), GDK_PIXBUF_ROTATE_CLOCKWISE);
g_object_unref(BACKEND_IMAGE(image));
BACKEND_IMAGE(image) = new_img;
public_info_update(image);
}


/* rotate image anti-clockwise, similarly. */
void backend_rotate_acw(xzgv_image *image)
{
GdkPixbuf *new_img = gdk_pixbuf_rotate_simple(BACKEND_IMAGE(image), GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
g_object_unref(BACKEND_IMAGE(image));
BACKEND_IMAGE(image) = new_img;
public_info_update(image);
}


/* create an image from RGB data, given width and height.
 * This version should do so *non-destructively*, by making a copy of
 * the data - the data passed to it should be left intact (and if it was
 * malloced, will need to later be freed by the caller).
 */
xzgv_image *backend_create_image_from_data(unsigned char *rgb,int w,int h)
{
unsigned char *rgbcopy;

/* no non-destructive version, so copy and use destructive one */
if((rgbcopy=malloc(w*h*3))==NULL)
  return(NULL);

memcpy(rgbcopy,rgb,w*h*3);
return(backend_create_image_from_data_destructively(rgbcopy,w,h));
}


/* create an image from RGB data, *destructively*.
 * This takes over the rgb data passed to it, such that a) the caller
 * should NOT free it, and b) the data may change (probably not now,
 * but perhaps later if we do a flip or something).
 *
 * *On error, the rgb data must be freed.* This is, after all, meant
 * to be destructive, such that the caller need not care about the
 * rgb data after. It also means that the rgb data MUST have been
 * malloced... :-)
 *
 * Obviously this version should be faster if the backend supports it;
 * if not, call backend_create_image_from_data() then free().
 */
xzgv_image *backend_create_image_from_data_destructively(unsigned char *rgb,
                                                         int w,int h)
{
GdkPixbuf *backim;
xzgv_image *im;

if((im=malloc(sizeof(xzgv_image)))==NULL)
  return(NULL);

if((backim=gdk_pixbuf_new_from_data(rgb,GDK_COLORSPACE_RGB,FALSE,8,
                                    w,h,w*3,
                                    (GdkPixbufDestroyNotify)free,NULL))==NULL)
  {
  free(im);
  free(rgb);	/* since it failed */
  return(NULL);
  }

backend_image_init(im);
im->backend_image=backim;
public_info_update(im);

return(im);
}

int backend_get_orientation_from_file(char *filename)
{
GdkPixbufFormat *imform;
gchar *format;
ExifData *ed;
ExifEntry *entry;
ExifShort orient;
static const ExifShort xzgv_orient[]={0,0,1,3,2,7,4,6,5};

if((imform=gdk_pixbuf_get_file_info(filename,NULL,NULL))==NULL) return 0;
if((format=gdk_pixbuf_format_get_name(imform))==NULL) return 0;
if(!strcmp(format, "jpeg") || !strcmp(format, "tiff"))
  {
  if((ed=exif_data_new_from_file(filename))==NULL) return 0;
  if((entry=exif_data_get_entry(ed,EXIF_TAG_ORIENTATION))==NULL) return 0;
  if((orient=exif_get_short(entry->data,exif_data_get_byte_order(ed)))>sizeof(xzgv_orient)-1) return 0;
  orient=xzgv_orient[orient];
  return (int)orient;
  }
return 0;
}

/* create an image from a given picture file.
 * The most important formats should eventually be dealt with by xzgv
 * directly, so it would be acceptable for this to just use netpbm or
 * ImageMagick to read the file, then use
 * backend_create_image_from_data_destructively()
 * on it. Or if the backend has a file reader which returns an image,
 * you could use that.
 *
 * This must return NULL (freeing image if necessary) if the picture
 * is larger than 32767 pixels in either dimension.
 */
xzgv_image *backend_create_image_from_file(char *filename)
{
GdkPixbuf *backim;
xzgv_image *im;
GError *gerror = NULL;

if((im=malloc(sizeof(xzgv_image)))==NULL)
  return(NULL);

/* XXX does this deal with the 32767 issue or not? */
if((backim=gdk_pixbuf_new_from_file((const char *)filename, &gerror))==NULL)
  {
  free(im);
  return(NULL);
  }

backend_image_init(im);
im->backend_image=backim;
public_info_update(im);

return(im);
}


/* render image at (x,y) in window (at actual size).
 * This is a fairly high-level one, but most backends will probably
 * support it, and xzgv does need it.
 * It should not leave any random pixmaps lying around. :-)
 */
void backend_render_image_into_window(xzgv_image *image,GdkWindow *win,
                                      int x,int y)
{
/* XXX this assumes only one window is rendered into */
static GdkGC *gc=NULL;

if(!gc)
  gc=gdk_gc_new(win);

gdk_draw_pixbuf(win,gc,BACKEND_IMAGE(image),
                0,0,x,y,image->w,image->h,
                dither_type,0,0);
}


/* render a pixmap from the image (at the given size), which is then
 * associated with it.
 *
 * returns 1 if this failed, else 0.
 *
 * (Use xzgv_image's `backend_ext' field to save the pixmap pointer.)
 */
int backend_render_pixmap_for_image(xzgv_image *image,int x,int y)
{
GdkPixbuf *backim;
GdkPixmap *pixmap;

backim=gdk_pixbuf_composite_color_simple(BACKEND_IMAGE(image),x,y,interp_type,
				255,1,0x000000,0xffffff);
if(backim==NULL)
  return(0);

gdk_pixbuf_render_pixmap_and_mask(backim,&pixmap,NULL,128);

if(image->backend_ext)		/* not normally the case */
  backend_pixmap_destroy((GdkPixmap *)image->backend_ext);

image->backend_ext=(void *)pixmap;

g_object_unref(backim);

return(1);
}


/* return the most recently rendered pixmap associated with the image,
 * de-associating it from the image. The returned pixmap should not be
 * modified. Returns NULL if there was no associated pixmap.
 */
GdkPixmap *backend_get_and_detach_pixmap(xzgv_image *image)
{
GdkPixmap *ret=image->backend_ext;

image->backend_ext=NULL;
return(ret);
}


/* free a pixmap generated by the backend.
 * (The assumption is there may be some caching system which will
 * be less than pleased if we don't do it the way it wants.)
 */
void backend_pixmap_destroy(GdkPixmap *pixmap)
{
g_object_unref(pixmap);
}


/* destroy image. */
void backend_image_destroy(xzgv_image *image)
{
if(image->backend_ext)
  backend_pixmap_destroy((GdkPixmap *)image->backend_ext);

if(image->backend_image)
  g_object_unref(BACKEND_IMAGE(image));

free(image);
}


/* get high-colour (15/16-bit) dithering status.
 * returns 1 if enabled, 0 if disabled, else -1 which indicates that
 * either the current visual does not support hicol dithering, or
 * this backend doesn't support it. (Since it's optional.)
 */
int backend_get_hicol_dither(void)
{
return (dither_type == GDK_RGB_DITHER_MAX) ? TRUE : FALSE;
}


/* set high-colour dithering status.
 * Should not be called if backend_get_hicol_dither() returned -1.
 */
void backend_set_hicol_dither(int on)
{
dither_type = on ? GDK_RGB_DITHER_MAX : GDK_RGB_DITHER_NORMAL;
}


int backend_get_interp(void)
{
return (interp_type == GDK_INTERP_BILINEAR) ? TRUE : FALSE;
}


void backend_set_interp(int on)
{
interp_type = on ? GDK_INTERP_BILINEAR : GDK_INTERP_NEAREST;
}


/* get colour which most closely matches arg's RGB fields
 * preferably setting those fields to actual RGB value of
 * colour returned (in col->pixel).
 */
void backend_get_closest_colour(GdkColor *col)
{
/* this seems to be the closest I can manage */
col->pixel=gdk_rgb_xpixel_from_rgb(
  (guint32)(((col->red>>8)<<16)|(col->green&0xff00)|(col->blue>>8)));
}


/* return visual currently being used.
 * should be able to use GDK call for this if need be, but this
 * call gives you the option to get it right for sure. :-)
 */
GdkVisual *backend_get_visual(void)
{
return(gdk_rgb_get_visual());
}


/* set value mapping to apply to all three colour channels when
 * rendering. While image *is* an arg here, a global setting would
 * be sufficient as long as it doesn't mangle already-rendered
 * pixmaps.
 */
void backend_set_value_mapping(xzgv_image *image,unsigned char *map)
{
/* XXX GDK seems not to have this. */
}


/* a fairly high-level one, which is unfortunately required:
 *
 * read XPM data from string array and render into pixmap, also returning
 * a bitmap mask matching any transparent parts. Any image used
 * on the way should be freed, making it a straight XPM to
 * pixmap/bitmap job.
 */
int backend_create_pixmap_from_xpm_data(const char **data,
                                        GdkPixmap **pixmap,GdkBitmap **mask)
{
GdkPixbuf *backim=gdk_pixbuf_new_from_xpm_data(data);

if(backim==NULL)
  {
  *pixmap=NULL;
  *mask=NULL;
  return(0);
  }

gdk_pixbuf_render_pixmap_and_mask(backim,pixmap,mask,128);

g_object_unref(backim);

return(1);
}
