# top-level Makefile for xzgv

# -----------------------------------------
# >>> NB: if you're looking to edit this to
# configure xzgv, edit `config.mk' instead.
# -----------------------------------------


# The main targets of interest are:
#
# all		the default; make everything except info
#		(it warns if the info is out of date, though)
# info		make info (requires texinfo's `makeinfo')
# install	install everything
# uninstall	can't imagine what use you could possibly have for this :^)
# clean		clean up
#
# tgz		make distribution tar.gz


# version number, needed for distrib-making stuff below.
#
VERS=0.8



all: src man infowarn

src: xzgv src/install-info

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
# So the `infowarn' below is mainly to warn me if the info gets
# out of date. :-)
info: doc/xzgv.gz

doc/xzgv.gz: doc/xzgv.texi
	cd doc && $(MAKE) info

# Warn if the info is out of date. This *is* automatically done.
# It's a bit kludgey though, using doc/xzgv-1.gz... :-)
infowarn: doc/xzgv-1.gz

doc/xzgv-1.gz: doc/xzgv.texi
	@echo '================================================'
	@echo 'WARNING: info files out of date, do "make info"!'
	@echo '================================================'


clean:
	cd src && $(MAKE) clean
	cd doc && $(MAKE) clean
	$(RM) *~

install: all
	cd src && $(MAKE) install
	cd doc && $(MAKE) install

uninstall:
	cd src && $(MAKE) uninstall
	cd doc && $(MAKE) uninstall


# The stuff below makes the distribution tgz.

dist: tgz
tgz: ../xzgv-$(VERS).tar.gz
  
# Based on the example in ESR's Software Release Practice HOWTO.
#
../xzgv-$(VERS).tar.gz: info man clean
	$(RM) ../xzgv-$(VERS)
	@cd ..;ln -s xzgv xzgv-$(VERS)
	cd ..;tar zchvf xzgv-$(VERS).tar.gz xzgv-$(VERS)
	@cd ..;$(RM) xzgv-$(VERS)
