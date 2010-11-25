CFLAGS = -O3 -Wall --std=c99 `pkg-config --cflags argtable2 sndfile slv2`
LDFLAGS = `pkg-config --libs argtable2 sndfile slv2`
BINDIR = $(DESTDIR)/usr/bin
INSTALL_PROGRAM = install

all: lv2file

lv2file.o: lv2file.c
	$(CC) -c $(CFLAGS) -o lv2file.o lv2file.c
lv2file: lv2file.o
	$(CC) $(LDFLAGS) lv2file.o -o lv2file
tarball: lv2file
	cd ..;tar -czvf lv2file.tar.gz lv2file/*;
.PHONY: install uninstall clean

clean:
	rm lv2file.o lv2file
install: all
	$(INSTALL_PROGRAM) -d $(BINDIR) $(DESTDIR)
	$(INSTALL_PROGRAM) lv2file $(BINDIR)/lv2file
uninstall:
	rm $(BINDIR)/lv2file
