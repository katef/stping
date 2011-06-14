# $Id$

# This is an example SOCK_DGRAM client-server application.
# To build with C-THRU sockets, build with:
#   CFLAGS=-DUSE_CTHRU make
#

#CC=cc
#CFLAGS+=

CC=gcc
CFLAGS+=-Wall -pedantic -ansi -O2

TARGETS=common.o

all: stping stpingd

clean:
	rm -f stping stpingd stping.o stpingd.o $(TARGETS)


stping: stping.o $(TARGETS)
	$(CC) -o stping stping.o $(TARGETS) -lm

stpingd: stpingd.o $(TARGETS)
	$(CC) -o stpingd stpingd.o $(TARGETS)

