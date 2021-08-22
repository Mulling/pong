CC := gcc

CFLAGS := -Wall -Wextra -Werror -DDEBUG -g

pong: main.c
	$(CC) $(CFLAGS) -o $@ $^

release: CFLAGS += -O2
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
