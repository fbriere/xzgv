/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * confirm.h
 */

extern void confirmation_dialog(char *title,char *msg,
                                void (*main_callback)(void));
