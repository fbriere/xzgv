# Makefile for xzgv docs
#
# Only info version is made by default, do `make dvi' to make a .dvi
# file.


# This gets definitions for INFODIR, MANDIR, etc.
include ../config.mk


all: info man

info: xzgv.gz
dvi: xzgv.dvi

xzgv.gz: xzgv.texi
	makeinfo xzgv.texi
	gzip -f xzgv

# `-c' removes the huge number of associated files created by TeX.
# This saves doing a `make clean' from hell. :-)
xzgv.dvi: xzgv.texi
	@echo "=== NB: If you don't want A4 paper, edit config.mk! ==="
	texi2dvi -c $(USE_A4_DEF) xzgv.texi

# This explicitly mentions `gawk' because makeman requires it
# (and can't easily be made not to, since it really does need gensub).
# To avoid problems on gawkless systems, a prebuilt xzgv.1 is included
# in the source tgz.
#
man: xzgv.1
xzgv.1: xzgv.texi makeman.awk
	@echo 'Making man page from xzgv.texi...'
	LANG= gawk -f makeman.awk <xzgv.texi >xzgv.1

installdirs:
	/bin/sh ../mkinstalldirs $(INFODIR) $(MANDIR)

install: installdirs
	install xzgv.1 $(MANDIR)
	install-info xzgv $(INFODIR)/dir

uninstall:
	$(RM) $(MANDIR)/xzgv.1
	install-info --remove xzgv

# This *doesn't* remove the Info files, which should stick around to
# be included in the distribution. Ditto for the man page, since
# building it requires gawk.
clean:
	$(RM) *~ xzgv.dvi
