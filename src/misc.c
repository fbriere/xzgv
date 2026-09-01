/* xzgv v0.2 - picture viewer for X, with file selector.
 * Copyright (C) 1999 Russell Marks. See main.c for license details.
 *
 * misc.c - miscellaneous util routines.
 */

/* XXX there are probably many other routines that should get shunted
 * out to this :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

#include "misc.h"


static gchar *current_dir;


/* a crude quit for when malloc and the like fails */
void quit_no_mem(void)
{
fprintf(stderr,"xzgv: out of memory\n");
exit(1);
}


void init_current_dir(void)
{
if (!current_dir)
  current_dir = g_get_current_dir();
}


char *xzgv_getcwd(char *buf,size_t size)
{
init_current_dir();
if (g_strlcpy(buf,current_dir,size) >= size)
  return NULL;
else
  return buf;
}


int xzgv_chdir(const char *path)
{
int retval;

init_current_dir();
gchar *target_dir = g_canonicalize_filename(path,current_dir);
retval = chdir(target_dir);

if (retval == 0) {
  g_free(current_dir);
  current_dir = target_dir;
} else {
  g_free(target_dir);
}

return retval;
}


/* a de-hassled getcwd(). You need to free the memory after use, though.
 * XXX should get all remaining getcwd()s in the code to use this...
 */
char *getcwd_allocated(void)
{
int incr=1024;
int size=incr;
char *buf=malloc(size);

while(buf!=NULL && xzgv_getcwd(buf,size-1)==NULL)
  {
  free(buf);
  size+=incr;
  buf=malloc(size);
  }

if(buf==NULL)
  quit_no_mem();

return(buf);
}
