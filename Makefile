PROG = $(shell pwd | xargs basename)

# CFLAGS = -g -pedantic -std=c99 -Wall -Werror -Wextra -D_GNU_SOURCE
CFLAGS = -O2 -D_GNU_SOURCE -std=c99

sources = $(wildcard *.c)
objects = $(patsubst %.c,%.o,$(sources))

$(PROG): $(objects)
	gcc -s -o $@ $^

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

clean:
	@echo cleaning...
	@rm -f *.o *~ core.*
