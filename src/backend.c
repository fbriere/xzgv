/* xzgv 0.7 - picture viewer for X, with file selector.
 * Copyright (C) 1999,2000 Russell Marks. See main.c for license details.
 *
 * backend.c - picture rendering and (to a certain extent) loading.
 *
 * This is intended to be a reasonably generic wrapper for the library
 * which is actually doing the work, to ease any transition from Imlib
 * 1.x to something else. (Or I could keep the Imlib 1.x backend in,
 * but add another and recommend using that, etc.)
 *
 * The basic assumptions are:
 *
 * - all pictures loaded as 24-bit. (So Imlib2 support'll be fun :-( )
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

#include "backend.h"




/* *******************************************************************
 * *******************************************************************
 * ************                                           ************
 * ************            Imlib 1.x backend              ************
 * ************                                           ************
 * *******************************************************************
 * *******************************************************************
 */
#ifdef BACKEND_IMLIB1

#define BACKEND_PRESENT		1

#include <gdk_imlib.h>

/* get backend image, casted to appropriate type */
#define BACKEND_IMAGE(x)	((GdkImlibImage *)((x)->backend_image))


/* do any initialisation the backend needs. Should include any
 * visual/colormap change required.
 * returns 1 if ok, else 0. (If 0, should output descriptive error msg.)
 */
int backend_init(void)
{
gdk_imlib_init();

/* Get gdk to use imlib's visual and colormap */
gtk_widget_push_visual(gdk_imlib_get_visual());
gtk_widget_push_colormap(gdk_imlib_get_colormap());

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
image->rgb=BACKEND_IMAGE(image)->rgb_data;
image->w=BACKEND_IMAGE(image)->rgb_width;
image->h=BACKEND_IMAGE(image)->rgb_height;
}


/* mark an image as `changed', i.e. `dirty' it. */
void backend_image_changed(xzgv_image *image)
{
gdk_imlib_changed_image(BACKEND_IMAGE(image));
}


/* flip image vertically. Should `dirty' image if needed.
 * Should also update xzgv_image's rgb/w/h fields (use public_info_update()).
 * This shouldn't require extra memory and so shouldn't fail, but if it
 * does, the func shouls give error and exit itself.
 */
void backend_flip_vert(xzgv_image *image)
{
gdk_imlib_flip_image_vertical(BACKEND_IMAGE(image));
public_info_update(image);
/* XXX does this need dirtying? if so, check other flip/rots */
}


/* flip image horizontally, similarly. */
void backend_flip_horiz(xzgv_image *image)
{
gdk_imlib_flip_image_horizontal(BACKEND_IMAGE(image));
public_info_update(image);
}


/* rotate image clockwise, similarly.
 * Of course, this probably *will* need extra memory; again, func should
 * give error and exit if it runs out.
 */
void backend_rotate_cw(xzgv_image *image)
{
/* imlib1's rotate function is rather strange. For now, assume d==0 will
 * always invoke the current behaviour (which is more a flip than a rotate),
 * and fix things after.
 */
gdk_imlib_rotate_image(BACKEND_IMAGE(image),0);
gdk_imlib_flip_image_horizontal(BACKEND_IMAGE(image));
public_info_update(image);
}


/* rotate image anti-clockwise, similarly.
 * If the backend doesn't have a native function for this, do it
 * with rotate/flip_v/flip_h.
 */
void backend_rotate_acw(xzgv_image *image)
{
/* similarly odd... */
gdk_imlib_rotate_image(BACKEND_IMAGE(image),0);
gdk_imlib_flip_image_vertical(BACKEND_IMAGE(image));
public_info_update(image);
}


/* create an image from RGB data, given width and height.
 * This version should do so *non-destructively*, by making a copy of
 * the data - the data passed to it should be left intact (and if it was
 * malloced, will need to later be freed by the caller).
 */
xzgv_image *backend_create_image_from_data(unsigned char *rgb,int w,int h)
{
GdkImlibImage *backim;
xzgv_image *im;

if((im=malloc(sizeof(xzgv_image)))==NULL)
  return(NULL);

if((backim=gdk_imlib_create_image_from_data(rgb,NULL,w,h))==NULL)
  {
  free(im);
  return(NULL);
  }

backend_image_init(im);
im->backend_image=backim;
public_info_update(im);

return(im);
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
#if 0
/* imlib1 doesn't have any func to do this directly :-( */

xzgv_image *ret=backend_create_image_from_data(rgb,w,h);

free(rgb);	/* whether it worked or not, since it copies the data */

return(ret);

#else

/* of course, given that Imlib1 is barely maintained these days,
 * and that such a basic routine is very unlikely to change,
 * we *could* just case and paste the ...from_data func, doing things
 * a bit more rationally. Evil, but faster. ;-) (Also saves memory.)
 */

xzgv_image *xzim;
GdkImlibImage *im;
char s[128];

if((xzim=malloc(sizeof(xzgv_image)))==NULL)
  {
  free(rgb);
  return(NULL);
  }

backend_image_init(xzim);

if((im=malloc(sizeof(GdkImlibImage)))==NULL)
  {
  free(rgb);
  free(xzim);
  return(NULL);
  }

im->map = NULL;
im->rgb_width = w;
im->rgb_height = h;
im->rgb_data = rgb;
im->alpha_data = NULL;

/* Imlib does this next bit for anonymous data... *wretch* */
g_snprintf(s, sizeof(s), "creat_%lx_%x", time(NULL), rand());
im->filename = strdup(s);

im->width = 0;
im->height = 0;
im->shape_color.r = -1;
im->shape_color.g = -1;
im->shape_color.b = -1;
im->border.left = 0;
im->border.right = 0;
im->border.top = 0;
im->border.bottom = 0;
im->pixmap = NULL;
im->shape_mask = NULL;
im->cache = 1;
/* RGB modifiers should be set before rendering, so we can get away with
 * leaving those out.
 */

xzim->backend_image=im;
public_info_update(xzim);

return(xzim);
#endif
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
GdkImlibImage *backim;
xzgv_image *im;

if((im=malloc(sizeof(xzgv_image)))==NULL)
  return(NULL);

/* Imlib1 deals with the 32767 thing itself, IIRC */
if((backim=gdk_imlib_load_image(filename))==NULL)
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
gdk_imlib_paste_image(BACKEND_IMAGE(image),win,x,y,image->w,image->h);
}


/* render a pixmap from the image (at the given size), which is then
 * associated with it.
 *
 * returns 1 if this failed, else 0.
 *
 * (Use xzgv_image's `backend_ext' field to save the pixmap pointer
 * if the backend has no concept of associated pixmaps. And if there's
 * a direct 1:1 render function, use that when the width/height matches
 * the image's width/height.)
 */
int backend_render_pixmap_for_image(xzgv_image *image,int x,int y)
{
return(gdk_imlib_render(BACKEND_IMAGE(image),x,y)?1:0);
}


/* return the most recently rendered pixmap associated with the image,
 * de-associating it from the image. The returned pixmap should not be
 * modified. Returns NULL if there was no associated pixmap.
 */
GdkPixmap *backend_get_and_detach_pixmap(xzgv_image *image)
{
return(gdk_imlib_move_image(BACKEND_IMAGE(image)));
}


/* free a pixmap generated by the backend.
 * (The assumption is there may be some caching system which will
 * be less than pleased if we don't do it the way it wants.)
 */
void backend_pixmap_destroy(GdkPixmap *pixmap)
{
gdk_imlib_free_pixmap(pixmap);
}


/* destroy image. */
void backend_image_destroy(xzgv_image *image)
{
if(image->backend_image)
  gdk_imlib_kill_image(BACKEND_IMAGE(image));

free(image);
}


/* get high-colour (15/16-bit) dithering status.
 * returns 1 if enabled, 0 if disabled, else -1 which indicates that
 * either the current visual does not support hicol dithering, or
 * this backend doesn't support it. (Since it's optional.)
 */
int backend_get_hicol_dither(void)
{
switch(gdk_imlib_get_render_type())
  {
  case RT_PLAIN_TRUECOL:	return(0);
  case RT_DITHER_TRUECOL:	return(1);
  }

return(-1);
}


/* set high-colour dithering status.
 * Should not be called if backend_get_hicol_dither() returned -1.
 */
void backend_set_hicol_dither(int on)
{
if(backend_get_hicol_dither()!=-1)	/* but just in case :-) */
  gdk_imlib_set_render_type(on?RT_DITHER_TRUECOL:RT_PLAIN_TRUECOL);
}


/* get colour which most closely matches arg's RGB fields
 * preferably setting those fields to actual RGB value of
 * colour returned (in col->pixel).
 */
void backend_get_closest_colour(GdkColor *col)
{
gdk_imlib_best_color_get(col);
}


/* return visual currently being used.
 * should be able to use GDK call for this if need be, but this
 * call gives you the option to get it right for sure. :-)
 */
GdkVisual *backend_get_visual(void)
{
return(gdk_imlib_get_visual());
}


/* set value mapping to apply to all three colour channels when
 * rendering. While image *is* an arg here, a global setting would
 * be sufficient as long as it doesn't mangle already-rendered
 * pixmaps.
 */
void backend_set_value_mapping(xzgv_image *image,unsigned char *map)
{
gdk_imlib_set_image_red_curve(BACKEND_IMAGE(image),map);
gdk_imlib_set_image_green_curve(BACKEND_IMAGE(image),map);
gdk_imlib_set_image_blue_curve(BACKEND_IMAGE(image),map);
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
return(gdk_imlib_data_to_pixmap((char **)data,pixmap,mask)?1:0);
}

#endif /* BACKEND_IMLIB1 */




/* *******************************************************************
 * *******************************************************************
 * ************                                           ************
 * ************           gdk-pixbuf backend              ************
 * ************                                           ************
 * *******************************************************************
 * *******************************************************************
 */
#ifdef BACKEND_GDK_PIXBUF

#define BACKEND_PRESENT		1

#include <gdk-pixbuf/gdk-pixbuf.h>

/* get backend image, casted to appropriate type */
#define BACKEND_IMAGE(x)	((GdkPixbuf *)((x)->backend_image))


/* do any initialisation the backend needs. Should include any
 * visual/colormap change required.
 * returns 1 if ok, else 0. (If 0, should output descriptive error msg.)
 */
int backend_init(void)
{
gdk_rgb_init();

gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
gtk_widget_set_default_visual(gdk_rgb_get_visual());

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
 * This shouldn't require extra memory and so shouldn't fail, but if it
 * does, the func shouls give error and exit itself.
 */
void backend_flip_vert(xzgv_image *image)
{
/* XXX NYI */
public_info_update(image);
}


/* flip image horizontally, similarly. */
void backend_flip_horiz(xzgv_image *image)
{
/* XXX NYI */
public_info_update(image);
}


/* rotate image clockwise, similarly.
 * Of course, this probably *will* need extra memory; again, func should
 * give error and exit if it runs out.
 */
void backend_rotate_cw(xzgv_image *image)
{
/* XXX NYI */
public_info_update(image);
}


/* rotate image anti-clockwise, similarly.
 * If the backend doesn't have a native function for this, do it
 * with rotate/flip_v/flip_h.
 */
void backend_rotate_acw(xzgv_image *image)
{
/* XXX NYI */
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

if((im=malloc(sizeof(xzgv_image)))==NULL)
  return(NULL);

/* XXX does this deal with the 32767 issue or not? */
if((backim=gdk_pixbuf_new_from_file((const char *)filename))==NULL)
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

gdk_pixbuf_render_to_drawable(BACKEND_IMAGE(image),win,gc,
                              0,0,x,y,image->w,image->h,
                              GDK_RGB_DITHER_NORMAL,x,y);
}


/* render a pixmap from the image (at the given size), which is then
 * associated with it.
 *
 * returns 1 if this failed, else 0.
 *
 * (Use xzgv_image's `backend_ext' field to save the pixmap pointer
 * if the backend has no concept of associated pixmaps. And if there's
 * a direct 1:1 render function, use that when the width/height matches
 * the image's width/height.)
 */
int backend_render_pixmap_for_image(xzgv_image *image,int x,int y)
{
GdkPixbuf *backim;
GdkPixmap *pixmap;
int same=0;

if(x==image->w && y==image->h)
  {
  same=1;
  backim=BACKEND_IMAGE(image);
  }
else
  {
  if((backim=gdk_pixbuf_scale_simple(BACKEND_IMAGE(image),x,y,
                                     GDK_INTERP_NEAREST))==NULL)
    return(0);
  }

gdk_pixbuf_render_pixmap_and_mask(backim,&pixmap,NULL,128);

if(image->backend_ext)		/* not normally the case */
  backend_pixmap_destroy((GdkPixmap *)image->backend_ext);

image->backend_ext=(void *)pixmap;

if(!same)
  gdk_pixbuf_unref(backim);

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
gdk_pixmap_unref(pixmap);
}


/* destroy image. */
void backend_image_destroy(xzgv_image *image)
{
if(image->backend_ext)
  backend_pixmap_destroy((GdkPixmap *)image->backend_ext);

if(image->backend_image)
  gdk_pixbuf_unref(BACKEND_IMAGE(image));

free(image);
}


/* get high-colour (15/16-bit) dithering status.
 * returns 1 if enabled, 0 if disabled, else -1 which indicates that
 * either the current visual does not support hicol dithering, or
 * this backend doesn't support it. (Since it's optional.)
 */
int backend_get_hicol_dither(void)
{
/* XXX NYI */
return(-1);
}


/* set high-colour dithering status.
 * Should not be called if backend_get_hicol_dither() returned -1.
 */
void backend_set_hicol_dither(int on)
{
/* XXX NYI */
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
/* XXX NYI - does it even have this!? */
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

/* "As a convenience, gdk-pixbuf also provides the
 * gdk_pixbuf_render_pixmap_and_mask() function; this will create new
 * pixmap and mask drawables for a whole pixbuf and render the image
 * data onto them. Only trivially simple applications should find a
 * use for this function, since usually you want finer control of how
 * things are rendered."
 *	-- 0.9.0's porting-from-imlib.sgml
 *
 * *boggle* So anything which just wants a pixmap is "trivially simple"
 * now? Who died and put *you* in charge?
 */
gdk_pixbuf_render_pixmap_and_mask(backim,pixmap,mask,128);

gdk_pixbuf_unref(backim);

return(1);
}

#endif /* BACKEND_GDK_PIXBUF */



/* check a backend was compiled in */
#ifndef BACKEND_PRESENT
#error "you need to choose a backend in config.mk"
#endif
