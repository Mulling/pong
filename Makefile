CC := gcc

CFLAGS := -Wall -Wextra -Werror -Wpedantic -DDEBUG -g -pipe

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: pong

pong: ${OBJ}
	$(CC) ${CFLAGS} -o $@ $^

release: CFLAGS += -O3
release: CFLAGS := $(filter-out -DDEBUG, $(CFLAGS))
release: CFLAGS := $(filter-out -g, $(CFLAGS))
release: pong
release:
	sudo chown root:root pong
	sudo chmod u+s pong

clean:
	rm -f pong
	rm -f *.o

.PHONY: clean release
.EXTRA_PREREQS := $(abspath $(lastword $(MAKEFILE_LIST)))
