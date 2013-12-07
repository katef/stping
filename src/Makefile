# $Id$

#CC=cc
#CFLAGS+=

CC=gcc
CFLAGS+=-Wall -pedantic -std=c99 -O2

TARGETS=common.o

all: stping stpingd

clean:
	rm -f stping stpingd stping.o stpingd.o $(TARGETS)


stping: stping.o $(TARGETS)
	$(CC) -o stping stping.o $(TARGETS) -lm $(LDFLAGS)

stpingd: stpingd.o $(TARGETS)
	$(CC) -o stpingd stpingd.o $(TARGETS) $(LDFLAGS)

