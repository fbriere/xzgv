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

#include "misc.h"


/* a crude quit for when malloc and the like fails */
void quit_no_mem(void)
{
fprintf(stderr,"xzgv: out of memory\n");
exit(1);
}


/* a de-hassled getcwd(). You need to free the memory after use, though.
 * XXX should get all remaining getcwd()s in the code to use this...
 */
char *getcwd_allocated(void)
{
int incr=1024;
int size=incr;
char *buf=malloc(size);

while(buf!=NULL && getcwd(buf,size-1)==NULL)
  {
  free(buf);
  size+=incr;
  buf=malloc(size);
  }

if(buf==NULL)
  quit_no_mem();

return(buf);
}
