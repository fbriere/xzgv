/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * resizepic.h
 */


extern unsigned char *resizepic(unsigned char *theimage,
                                int width,int height,int *sw_ask,int *sh_ask,int allow_crunch);
