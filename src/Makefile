# $Id$

#CC=cc
#CFLAGS=

CC=gcc
CFLAGS=-Wall -pedantic -ansi -O2

TARGETS=common.o

all: dgping dgpingd

clean:
	rm -f dgping dgpingd dgping.o dgpingd.o $(TARGETS)


dgping: dgping.o $(TARGETS)
	$(CC) -o dgping dgping.o $(TARGETS) -lm

dgpingd: dgpingd.o $(TARGETS)
	$(CC) -o dgpingd dgpingd.o $(TARGETS)

