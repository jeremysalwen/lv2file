CC = gcc
CFLAGS = -O3 -mtune=native -Wall --std=c99 `pkg-config --cflags argtable2 sndfile slv2`
LDFLAGS = `pkg-config --libs argtable2 sndfile slv2`
all: lv2file

lv2file.o: lv2file.c
	$(CC) -c $(CFLAGS) -o lv2file.o lv2file.c

lv2file: lv2file.o
	$(CC) $(LDFLAGS) lv2file.o -o lv2file $(LDLIBS)
