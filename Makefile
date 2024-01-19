CFLAGS := -Wall -Wextra -Werror -Wpedantic -DDEBUG -g -pipe -fsanitize=address -fsanitize=undefined

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: pong

pong: ${OBJ}
	$(CC) ${CFLAGS} -o $@ $^

release: CFLAGS = -O3 -march=native -mtune=native
release: all
	strip pong
	sudo setcap 'cap_net_raw+ep' pong

clean:
	rm -f pong *.o

.PHONY: all clean release
.EXTRA_PREREQS := $(abspath $(lastword $(MAKEFILE_LIST)))
