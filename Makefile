.DEFAULT_GOAL := all

CC=gcc
CFLAGS=-Wall -I. -lev -lpam $(shell pkg-config --cflags --libs cairo xcb xcb-image \
					       	  xcb-xkb xkbcommon xkbcommon-x11 xcb-icccm xcb-randr)
INSTALL_PROGRAM=install
DEPS =

%.o: %.c $(DEPS)
	      $(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f main.o randr.o block

install:
	$(INSTALL_PROGRAM) -D -m 755 block $(DESTDIR)/usr/local/bin
	$(INSTALL_PROGRAM) -D -m 644 pam/block $(DESTDIR)/etc/pam.d/

uninstall:
	rm $(DESTDIR)/usr/local/bin/block
	rm $(DESTDIR)/etc/pam.d/block

all: main.o randr.o
	 $(CC) -o block $(CFLAGS) main.o randr.o
