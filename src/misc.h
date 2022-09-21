/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * misc.c
 */

extern void quit_no_mem(void);
extern int xzgv_chdir(const char *path);
extern char *xzgv_getcwd(char *buf,size_t size);
extern char *getcwd_allocated(void);
