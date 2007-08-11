/* xzgv 0.8 - picture viewer for X, with file selector.
 * Copyright (C) 1999-2004 Russell Marks. See main.c for license details.
 *
 * reader.h
 */

/* range check on width and height as a crude way of avoiding overflows
 * when calling malloc/calloc. 32767 is the obvious limit to use given that
 * xzgv effectively imposes such a limit anyway.
 * Adds an extra 2 to height for max-height check, partly to reflect what
 * the check in zgv does but also to allow for readtiff.c allocating an
 * extra line (so at least an extra 1 would have been needed in any case).
 */
#define WH_MAX	32767
#define WH_BAD(w,h)	((w)<=0 || (w)>WH_MAX || (h)<=0 || ((h)+2)>WH_MAX)
