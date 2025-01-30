include config.mk

CFLAGS ?= ${PONGFLAGS}

debug: CFLAGS := ${PONG_DEBUG_FLAGS} ${CFLAGS}
debug: pong

main.o: main.c
	$(QUIET_CC)$(CC) ${CFLAGS} -c $^ -o $@

pong: main.o
	$(QUIET_CC)$(CC) ${CFLAGS} -o $@ $^

clean:
	$(QUIET_RM)$(RM) -f pong *.o

$(DESTDIR)$(PREFIX)/pong: pong
	$(QUIET_MKDIR) mkdir -p $(DESTDIR)$(PREFIX)
	$(QUIET_CP) cp -f pong $(DESTDIR)$(PREFIX)
	$(QUIET_STRIP) strip $(DESTDIR)$(PREFIX)/pong
	$(QUIET_SETCAP) setcap 'cap_net_raw+ep' $(DESTDIR)$(PREFIX)/pong

install: $(DESTDIR)$(PREFIX)/pong

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/pong

.PHONY: clean debug install uninstall
.EXTRA_PREREQS := $(abspath $(lastword $(MAKEFILE_LIST)))
