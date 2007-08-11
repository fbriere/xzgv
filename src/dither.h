/* xzgv v0.1 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * dither.h
 */

extern int ditherinit(int w);
extern void ditherfinish(void);
extern void ditherline(unsigned char *theline,int linenum,int width);
