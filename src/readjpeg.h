/* xzgv 0.8 - picture viewer for X, with file selector.
 * Copyright (C) 1999-2003 Russell Marks. See main.c for license details.
 *
 * readjpeg.h
 */

extern int get_exif_orientation(char *filename);
extern int read_jpeg_file(char *filename,unsigned char **imagep,
                          int *wp,int *hp,int *origwp,int *orighp,int for_tn);
