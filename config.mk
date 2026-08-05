# dwm version
VERSION = 6.5

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# Xinerama, comment if you don't want it
XINERAMALIBS  = -lXinerama
XINERAMAFLAGS = -DXINERAMA

# freetype
FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = /usr/include/freetype2
# OpenBSD (uncomment)
#FREETYPEINC = ${X11INC}/freetype2
#MANPREFIX = ${PREFIX}/man

# Multimedia support. Remove a feature flag and its pkg-config package to
# disable that backend at build time. Brightness sysfs support needs no library.
MULTIMEDIAFLAGS = -DMULTIMEDIA_ALSA -DMULTIMEDIA_DBUS -DMULTIMEDIA_XRANDR
MULTIMEDIAPKG = alsa gio-2.0 xrandr
MULTIMEDIAINC = `pkg-config --cflags ${MULTIMEDIAPKG}`
MULTIMEDIALIBS = `pkg-config --libs ${MULTIMEDIAPKG}`

# includes and libs
INCS = -I${X11INC} -I${FREETYPEINC} ${MULTIMEDIAINC}
LIBS = -L${X11LIB} -lX11 ${XINERAMALIBS} ${FREETYPELIBS} ${MULTIMEDIALIBS}

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700L -DVERSION=\"${VERSION}\" ${XINERAMAFLAGS} ${MULTIMEDIAFLAGS}
#CFLAGS   = -g -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
CFLAGS   = -std=c99 -pedantic -Wall -Wno-deprecated-declarations -Os ${INCS} ${CPPFLAGS}
LDFLAGS  = ${LIBS}

# Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\"
#LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
