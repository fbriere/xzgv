# src Makefile for xzgv

# --------------------------------------------
# >>> NB: if you're looking to edit this to
# configure xzgv, edit `../config.mk' instead.
# --------------------------------------------


# This gets definitions for CC, CFLAGS, BINDIR etc.
include ../config.mk

CFLAGS+=`pkg-config --cflags gtk+-2.0`
LDFLAGS+=`pkg-config --cflags --libs gtk+-2.0`

GDK_PIXBUF_CFLAGS=`pkg-config --cflags gdk-pixbuf-2.0`
GDB_PIXBUF_LIBS=`pkg-config --libs gdk-pixbuf-2.0`

# `all' makes install-info too, as it's a bit weird to have something
# compile when you do `make install'. :-)
# (It would also be weird to have it made by the `doc' Makefile, IMHO.)

all: xzgv install-info

OBJS=	main.o \
	filedetails.o gotodir.o updatetn.o confirm.o help.o \
	copymove.o rename.o \
	resizepic.o dither.o \
	rcfile.o misc.o \
	getopt.o getopt1.o \
	backend.o

xzgv: $(OBJS)
	$(CC) $(LDFLAGS) -o xzgv $(OBJS) $(GDK_PIXBUF_LIBS) -ljpeg -lpng -ltiff -lz

backend.o:
	$(CC) $(CFLAGS) $(GDK_PIXBUF_CFLAGS) -c -o backend.o backend.c

install-info: install-info.c getopt.o getopt1.o
	$(CC) $(INFODIRDEF) -o install-info install-info.c getopt.o getopt1.o

installdirs:
	/bin/sh ../mkinstalldirs $(BINDIR)

install: xzgv installdirs
	install xzgv $(BINDIR)
	chmod 755 $(BINDIR)/xzgv

uninstall:
	$(RM) $(BINDIR)/xzgv

clean:
	$(RM) *~ *.o xzgv install-info
	$(RM) rcfile_opt.h rcfile_var.h rcfile_short.h


# dependancies
backend.o: backend.c backend.h
confirm.o: confirm.c backend.h main.h confirm.h
copymove.o: copymove.c backend.h main.h copymove.h
dither.o: dither.c dither.h
filedetails.o: filedetails.c backend.h main.h filedetails.h
getopt.o: getopt.c
getopt1.o: getopt1.c getopt.h
gotodir.o: gotodir.c backend.h main.h gotodir.h
help.o: help.c backend.h main.h rcfile.h help.h
install-info.o: install-info.c getopt.h
main.o: main.c backend.h resizepic.h rcfile.h filedetails.h gotodir.h \
 updatetn.h confirm.h misc.h copymove.h rename.h help.h dir_icon.xpm \
 dir_icon_small.xpm file_icon.xpm file_icon_small.xpm logo.h \
 icon-48.xpm main.h
misc.o: misc.c misc.h
rcfile.o: rcfile.c getopt.h rcfile.h rcfile_opt.h rcfile_var.h \
 rcfile_short.h
rename.o: rename.c backend.h main.h rename.h
resizepic.o: resizepic.c resizepic.h
updatetn.o: updatetn.c backend.h main.h rcfile.h dither.h resizepic.h \
 confirm.h misc.h updatetn.h

# more involved dependancy stuff
rcfile_opt.h rcfile_var.h rcfile_short.h: options.src mkopts.awk
	$(AWK) -f mkopts.awk <options.src

loc:
	sloccount *.c *.h