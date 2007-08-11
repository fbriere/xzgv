/* xzgv 0.6 - picture viewer for X, with file selector.
 * Copyright (C) 2000 Russell Marks. See main.c for license details.
 *
 * readpng.h
 */

extern int read_png_file(char *filename,unsigned char **theimageptr,
                         int *wp,int *hp);
