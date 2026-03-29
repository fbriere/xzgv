# This file configures xzgv's Makefiles. You should edit the settings
# below as needed.

# ---------------------- Compilation options ----------------------

# Set the C compiler to use, and options for it.
# This is likely to be what you'll want for most systems:
#
CC=gcc
CFLAGS=-O2 -Wall

# Set the awk interpreter to use for a script used while compiling.
# (This should be a `new' awk, such as gawk or mawk.)
#
# This setting should work for Linux and *BSD. On Solaris, you
# should set it to use `nawk' instead.
#
AWK=awk

# GTK version to compile for (either 2.0 or 3.0)
GTK_VERSION=3.0

# --------------------- Installation options ----------------------

# Set BINDIR to directory for binaries,
# INFODIR to directory for info files,
# MANDIR to directory for man page.
# Usually it will be simpler to just set PREFIX.
#
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
INFODIR=$(PREFIX)/share/info
MANDIR=$(PREFIX)/share/man/man1
DESKTOPDIR1=$(PREFIX)/share/applications
DESKTOPDIR2=$(PREFIX)/share/app-install/desktop
PIXMAPDIR=$(PREFIX)/share/pixmaps

# -------------------- Miscellaneous options -----------------------

# Finally, an option for `make dvi' in the `doc' directory. You only need
# worry about what this is set to if you plan to make a printed manual.
#
# Normally the .dvi file created will be formatted for printing on A4
# paper (210x297mm, 8.27x11.69"); if you'll be printing on non-A4,
# you'll probably want to comment this out. (It'll still warn you to
# edit this file when you do `make dvi', but that's only because
# doc/Makefile isn't as smart about that as it should be. :-))
#
USE_A4_DEF=-t @afourpaper
