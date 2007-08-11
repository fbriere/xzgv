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

# There's an optional optimisation for x86-based machines:
#
# xzgv can do its `scaling with interpolation' rather faster by taking
# advantage of MMX ops in a hotspot. This has no impact on non-MMX
# processors (it falls back to a conventional approach at runtime), so
# it's safe to leave it enabled on any x86-based machine.
#
# On non-x86-based machines (e.g. Alpha, Sparc, PPC), you should
# comment it out.
#
CFLAGS+=-DINTERP_MMX

# Set rendering backend to use. Currently only Imlib 1.x is properly
# supported, so leave this alone. :-)
#
BACKEND=IMLIB1
#BACKEND=GDK_PIXBUF
# (An Imlib2 backend may be added eventually, but it would be
# problematic as that uses RGBA rather than RGB.)


# --------------------- Installation options ----------------------

# Set BINDIR to directory for binaries,
# INFODIR to directory for info files,
# MANDIR to directory for man page.
# Usually it will be simpler to just set PREFIX.
#
PREFIX=/usr/local

# In theory it would be nice to put the info file and man page under
# /usr/local/share. However, it's not clear if this is widely
# supported yet, so for now the default is the traditional
# /usr/local/info and /usr/local/man/man1.
#
# If you want, though, or if you're installing with PREFIX=/usr,
# you can uncomment the following to get more FHS-like dirs such as
# /usr/local/share/info and /usr/local/share/man/man1.
#
# If you don't know what to do, leave it as-is.
#
#SHARE_INFIX=/share

BINDIR=$(PREFIX)/bin
INFODIR=$(PREFIX)$(SHARE_INFIX)/info
MANDIR=$(PREFIX)$(SHARE_INFIX)/man/man1

# Normally `make install' will update your `dir' file (in INFODIR),
# using a copy of texinfo's `install-info' bundled with xzgv.
#
# But if you have a different way of keeping `dir' up-to-date (for
# example, perhaps your setup automatically handles this for you) you
# should uncomment this to prevent `make install' doing that. However,
# if you're installing in /usr/local, it's possible any automated
# update deliberately doesn't mess with /usr/local/info/dir...
#
# If you don't know what to do, leave it as-is.
#
#INFO_DIR_UPDATE=no


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
