INSTALL_PREFIX=/usr/local

CFLAGS=`pkg-config fuse --cflags` `pkg-config glib --cflags`
LDFLAGS=`pkg-config fuse --libs` `pkg-config glib --libs`

all: sharebox.o git-annex.o
	gcc -g -Wall $(LDFLAGS) -o sharebox sharebox.o git-annex.o

sharebox.o: sharebox.c
	gcc -g -Wall $(CFLAGS) -c sharebox.c

git-annex.o: git-annex.c git-annex.h
	gcc -g -Wall $(CFLAGS) -c git-annex.c

test: all
	make -C tests/

clean:
	rm -f sharebox *.o

install: all
	install -Dm755 ./sharebox $(INSTALL_PREFIX)/bin/sharebox
	install -Dm755 ./mkfs.sharebox $(INSTALL_PREFIX)/sbin/mkfs.sharebox

uninstall:
	rm -f $(INSTALL_PREFIX)/bin/sharebox
	rm -f $(INSTALL_PREFIX)/bin/mkfs.sharebox
