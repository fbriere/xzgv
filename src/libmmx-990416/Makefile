## The following targets are defined ##########################################
#	all (default)		Make "examples install".
#
#	examples		Make and run the Examples/testit application.
#	install			Install the header file (via cp) to the install
#				directory defined below.
#
#	uninstall		Uninstall the header file from the install
#				directory defined below.


## Install Directories and Options ############################################
# These should be edited for your system...

# INCDIR is where the makefile will install/uninstall the user header file
# "mmx.h".
INCDIR=/usr/local/include

# Set this to your compiler, or override on the make command line.
CC=/usr/bin/gcc

# Set this to your C preprocessor, or override on the make command line.
CPP=/usr/bin/cpp

# I claim that a C preprocessor is more generally useful tool than one might
# suspect, and should therefore be available as /usr/bin/cpp.  However, some
# systems are guilty of hiding it (e.g. Red Hat Linux), so here is an
# alternative:
#CPP=/usr/bin/gcc -E

# Compiling with MMX_TRACE defined will cause each libmmx function to print
# debugging information.
#  Uncomment one of the following two lines:
CPPFLAGS=
#CPPFLAGS=-DMMX_TRACE



## DO NOT CHANGE ANYTHING BELOW THIS LINE #####################################

all: examples install

examples: mmxtest.s
	${CC} -Wall -O -o mmxtest mmxtest.s

mmxtest.s: mmxtest.i
	${CC} -Wall -O -S mmxtest.i

mmxtest.i: mmx.h mmxtest.c
	${CPP} -Wall ${CPPFLAGS} mmxtest.c > mmxtest.i

install:
	mkdir -p ${INCDIR}
	cp mmx.h ${INCDIR}

uninstall:
	rm -f ${INCDIR}/mmx.h

clean:
	rm -f mmxtest core

distclean: clean
	rm -f mmxtest.s mmxtest.i

