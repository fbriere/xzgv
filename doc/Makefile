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
	gzip -f xzgv xzgv-?

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
	gawk -f makeman.awk <xzgv.texi >xzgv.1

installdirs:
	/bin/sh ../mkinstalldirs $(INFODIR) $(MANDIR)

install: ../src/install-info installdirs
	install xzgv.1 $(MANDIR)
	install xzgv.gz xzgv-?.gz $(INFODIR)
	chmod 644 $(MANDIR)/xzgv.1 $(INFODIR)/xzgv*
# Update info `dir' file.
# Info always uses a dir file in preference to a dir.gz, so we don't use
# dir.gz unless it's the only game in town.
	if [ "$(INFO_DIR_UPDATE)" != no ]; then \
	  gzip -d xzgv.gz; \
	  if [ -f $(INFODIR)/dir.gz -a ! -f $(INFODIR)/dir ]; then \
	    gzip -d $(INFODIR)/dir.gz; \
	    ../src/install-info xzgv $(INFODIR)/dir; \
	    gzip $(INFODIR)/dir; \
	  else \
	    ../src/install-info xzgv $(INFODIR)/dir; \
	  fi; \
	  chmod a+r $(INFODIR)/dir; \
	  gzip xzgv; \
	fi

../src/install-info: ../src/install-info.c
	cd ../src && $(MAKE) install-info

# can't easily fix dir :-/, but do remove the files.
uninstall:
	$(RM) $(MANDIR)/xzgv.1
	$(RM) $(INFODIR)/{xzgv.gz,xzgv-?.gz}

# This *doesn't* remove the Info files, which should stick around to
# be included in the distribution. Ditto for the man page, since
# building it requires gawk.
clean:
	$(RM) *~ xzgv.dvi
