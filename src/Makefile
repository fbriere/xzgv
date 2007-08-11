# src Makefile for xzgv

# --------------------------------------------
# >>> NB: if you're looking to edit this to
# configure xzgv, edit `../config.mk' instead.
# --------------------------------------------


# This gets definitions for CC, CFLAGS, BINDIR etc.
include ../config.mk

CFLAGS+=-DBACKEND_$(BACKEND) `gtk-config --cflags`

# I could do this less horribly if I made it GNU-make-specific,
# but I decided not to. And here we are. :-(
#
# This stuff assumes the *-config programs call gtk-config in the
# `--libs' case.
#
BACKEND_CFLAGS=`case "$(BACKEND)" in \
	IMLIB1) imlib-config --cflags-gdk ;; \
	GDK_PIXBUF) gdk-pixbuf-config --cflags ;; \
	esac`
BACKEND_LIBS=`case "$(BACKEND)" in \
	IMLIB1) imlib-config --libs-gdk ;; \
	GDK_PIXBUF) gdk-pixbuf-config --libs ;; \
	esac`

# `all' makes install-info too, as it's a bit weird to have something
# compile when you do `make install'. :-)
# (It would also be weird to have it made by the `doc' Makefile, IMHO.)

all: xzgv install-info

OBJS=	main.o \
	filedetails.o gotodir.o updatetn.o confirm.o help.o \
	copymove.o rename.o \
	readmrf.o readgif.o readpng.o readjpeg.o \
	readtiff.o readprf.o \
	resizepic.o dither.o \
	rcfile.o misc.o logo.o \
	getopt.o getopt1.o \
	backend.o

xzgv: $(OBJS)
	$(CC) $(CFLAGS) -o xzgv $(OBJS) $(BACKEND_LIBS) -ljpeg -lpng -ltiff -lz

backend.o:
	$(CC) $(CFLAGS) $(BACKEND_CFLAGS) -c -o backend.o backend.c

logoconv: logoconv.o
	$(CC) $(CFLAGS) -o logoconv logoconv.o

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
	$(RM) logodata.h logoconv


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
logo.o: logo.c logodata.h
logoconv.o: logoconv.c
main.o: main.c backend.h readmrf.h readgif.h readpng.h readjpeg.h \
 readtiff.h readprf.h resizepic.h rcfile.h filedetails.h gotodir.h \
 updatetn.h confirm.h misc.h copymove.h rename.h help.h dir_icon.xpm \
 dir_icon_small.xpm file_icon.xpm file_icon_small.xpm logo.h \
 icon-48.xpm main.h
misc.o: misc.c misc.h
rcfile.o: rcfile.c getopt.h rcfile.h rcfile_opt.h rcfile_var.h \
 rcfile_short.h
readgif.o: readgif.c reader.h readgif.h
readjpeg.o: readjpeg.c rcfile.h reader.h readjpeg.h
readmrf.o: readmrf.c reader.h readmrf.h
readpng.o: readpng.c readpng.h
readprf.o: readprf.c reader.h readprf.h
readtiff.o: readtiff.c reader.h readtiff.h
rename.o: rename.c backend.h main.h rename.h
resizepic.o: resizepic.c resizepic.h
updatetn.o: updatetn.c backend.h main.h rcfile.h dither.h resizepic.h \
 confirm.h misc.h updatetn.h

# more involved dependancy stuff
logodata.h: logoconv logo.ppm
	./logoconv <logo.ppm >logodata.h
rcfile_opt.h rcfile_var.h rcfile_short.h: options.src mkopts.awk
	$(AWK) -f mkopts.awk <options.src
