TARGETS = getscancodes evmap xi2watch
SOURCES = $(addsuffix .c,$(TARGETS))
OBJECTS = $(SOURCES:.c=.o) key_names.inc

$(info SOURCES=$(SOURCES))

CC=c99
CFLAGS=-Wall -Wextra -g -O0

all: $(TARGETS)
clean:; $(RM) $(TARGETS) $(OBJECTS)

getscancodes: getscancodes.c

key_names.inc: $(FORCE)
	gcc -xc -E -dM - <<< '#include <linux/input-event-codes.h>' |sort -k3n |	\
	    perl -ne 'print qq|  { $$1, "$$2" },\n| if m/^#define (KEY_(\w+))\s+\S+/'	\
	    > $@
evmap.c: key_names.inc
evmap: CFLAGS+=-D_XOPEN_SOURCE=600
evmap: evmap.c

xi2watch: LDLIBS+=-lX11 -lXi
xi2watch: xi2watch.c
