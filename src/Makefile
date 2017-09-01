# src Makefile for xzgv

# --------------------------------------------
# >>> NB: if you're looking to edit this to
# configure xzgv, edit `../config.mk' instead.
# --------------------------------------------


# This gets definitions for CC, CFLAGS, BINDIR etc.
include ../config.mk

CFLAGS+=`pkg-config --cflags gtk+-2.0` `pkg-config --cflags gdk-pixbuf-2.0` \
	`pkg-config --cflags x11` `pkg-config --cflags libexif`
LDFLAGS+=`pkg-config --libs gtk+-2.0` `pkg-config --libs gdk-pixbuf-2.0` \
	`pkg-config --libs x11` `pkg-config --libs libexif` -lm

all: xzgv

OBJS=	main.o \
	filedetails.o gotodir.o updatetn.o confirm.o help.o \
	copymove.o rename.o \
	resizepic.o dither.o \
	rcfile.o misc.o \
	getopt.o getopt1.o \
	backend.o

xzgv: $(OBJS)
	$(CC) -o xzgv $(OBJS) $(LDFLAGS)

installdirs:
	/bin/sh ../mkinstalldirs $(BINDIR)

install: xzgv installdirs
	install xzgv $(BINDIR)
	chmod 755 $(BINDIR)/xzgv
	install xzgv.desktop $(DESKTOPDIR1)
	install xzgv.desktop $(DESKTOPDIR2)
	install icon-64.xpm $(PIXMAPDIR)/xzgv.xpm
	chmod 644 $(DESKTOPDIR1)/xzgv.desktop
	chmod 644 $(DESKTOPDIR2)/xzgv.desktop
	chmod 644 $(PIXMAPDIR)/xzgv.xpm

uninstall:
	$(RM) $(BINDIR)/xzgv
	$(RM) $(DESKTOPDIR1)/xzgv.desktop
	$(RM) $(DESKTOPDIR2)/xzgv.desktop
	$(RM) $(PIXMAPDIR)/xzgv.xpm

clean:
	$(RM) *~ *.o xzgv
	$(RM) rcfile_opt.h rcfile_var.h rcfile_short.h

realclean: clean

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
main.o: main.c backend.h resizepic.h rcfile.h filedetails.h gotodir.h \
 updatetn.h confirm.h misc.h copymove.h rename.h help.h dir_icon.xpm \
 dir_icon_small.xpm file_icon.xpm file_icon_small.xpm \
 icon-48.xpm xzgv.desktop main.h
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
