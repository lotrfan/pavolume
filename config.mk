NAME = pavol
VERSION = 0.1

# Customize below to fit your system

#DEBUGFLAGS=
DEBUGFLAGS=-g

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

# includes and libs
INCS = -I. -I/usr/include
LIBS = -L/usr/lib -lc -lpulse

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = ${DEBUGFLAGS} -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
LDFLAGS = ${DEBUGFLAGS} ${LIBS}

# compiler and linker
CC = cc
