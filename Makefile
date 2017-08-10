# top-level Makefile for xzgv

# -----------------------------------------
# >>> NB: if you're looking to edit this to
# configure xzgv, edit `config.mk' instead.
# -----------------------------------------


# The main targets of interest are:
#
# all		the default; make everything except info
# info		make info (requires texinfo's `makeinfo')
# install	install everything
# uninstall	can't imagine what use you could possibly have for this :^)
# clean		clean up
#
# tgz		make distribution tar.gz


# version number, needed for distrib-making stuff below.
#
VERS=0.9.2



all: src man

src: xzgv

# We try this the whole time, as the dependancies are a bit
# complicated to duplicate here.
xzgv:
	cd src && $(MAKE) xzgv

src/install-info: src/install-info.c
	cd src && $(MAKE) install-info

man: doc/xzgv.1

doc/xzgv.1: doc/xzgv.texi doc/makeman.awk
	cd doc && $(MAKE) xzgv.1

# Like in GNU stuff, info files aren't automatically remade,
# as I don't want to assume everyone has texinfo's `makeinfo' handy.
info: doc/xzgv.info.gz

doc/xzgv.info.gz: doc/xzgv.texi
	cd doc && $(MAKE) info

clean:
	cd src && $(MAKE) clean
	cd doc && $(MAKE) clean
	$(RM) *~

realclean:
	cd src && $(MAKE) realclean
	cd doc && $(MAKE) realclean
	$(RM) *~

install: all
	cd src && $(MAKE) install
	cd doc && $(MAKE) install

uninstall:
	cd src && $(MAKE) uninstall
	cd doc && $(MAKE) uninstall


# The stuff below makes the distribution tgz.

dist: ../xzgv-$(VERS).tar.gz

# Based on the example in ESR's Software Release Practice HOWTO.
#
../xzgv-$(VERS).tar.gz: info man realclean
	$(RM) ../xzgv-$(VERS)
	@cd ..;ln -s xzgv xzgv-$(VERS)
	cd ..;tar zchf xzgv-$(VERS).tar.gz --exclude=.svn xzgv-$(VERS)
	@cd ..;$(RM) xzgv-$(VERS)
