PONGFLAGS = -Wall -Wextra -Werror -Wpedantic -O3 -flto -pipe
PONG_DEBUG_FLAGS := -DDEBUG                      \
	                -g                           \
                    -fsanitize=address,undefined \
	                -fanalyzer

PREFIX ?= /usr/bin

ifndef VERBOSE
.SILENT:
QUIET_CP        = @echo ' INSTALL ' $@;
QUIET_CC	    = @echo ' CC      ' $@;
QUIET_LINK	    = @echo ' LINK    ' $@;
QUIET_AR	    = @echo ' AR      ' $@;
QUIET_RANLIB	= @echo ' RANLIB  ' $@;
QUIET_PERF      = @echo ' PERF    ' $(firstword $^);
QUIET_BEAR      = @echo ' BEAR    ' $@;
QUIET_RM        = @echo ' CLEAN   ' $(shell pwd);
QUIET_TEST      = @echo ' TEST    ' $^;
QUIET_STRIP     = @echo ' STRIP   ' $@;
QUIET_MKDIR     = @echo ' MKDIR   ' $(shell dirname $@);
QUIET_SETCAP    = @echo ' SETCAP  ' $@;
endif
