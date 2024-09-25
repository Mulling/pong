PONGFLAGS := -Wall -Wextra -Wpedantic -DDEBUG -g -pipe -fsanitize=address -fsanitize=undefined ${CFLAGS}

PREFIX ?= /usr/bin

all: pong

main.o: main.c
	$(CC) ${CFLAGS} -c $^ -o $@

pong: main.o
	$(CC) ${CFLAGS} -o $@ $^

clean:
	$(RM) -f pong *.o

release: PONGFLAGS = -Wall -Wextra -Werror -Wpedantic -O3 -march=native -mtune=native ${CFLAGS}
release: pong
	strip pong
	setcap 'cap_net_raw+ep' pong

install: release
	cp -f pong $(DESTDIR)$(PREFIX)
	setcap 'cap_net_raw+ep' $(DESTDIR)$(PREFIX)/pong

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/pong

.PHONY: clean release install uninstall
.EXTRA_PREREQS := $(abspath $(lastword $(MAKEFILE_LIST)))
