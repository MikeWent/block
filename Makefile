.DEFAULT_GOAL := all

CC=gcc
CFLAGS=-Wall -I. -lev -lpam $(shell pkg-config --cflags --libs cairo xcb xcb-image \
					       	  xcb-xkb xkbcommon xkbcommon-x11 xcb-icccm xcb-randr)
DEPS =

%.o: %.c $(DEPS)
	      $(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f main.o randr.o block

install:
	install -m 755 block /usr/local/bin
	install -m 644 pam/block /etc/pam.d/

uninstall:
	rm /usr/local/bin/block
	rm /etc/pam.d/block

all: main.o randr.o
	 $(CC) -o block $(CFLAGS) main.o randr.o
