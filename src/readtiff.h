/* xzgv 0.7 - picture viewer for X, with file selector.
 * Copyright (C) 2000 Russell Marks. See main.c for license details.
 *
 * readtiff.h
 */

extern int read_tiff_file(char *filename,unsigned char **imagep,
                          int *wp,int *hp);
