CFLAGS = -Wall -pedantic-errors
DEBUF_FLAGS = -g3

all: shell

shell:
	gcc shell.c $(CFLAGS) $(DEBUF_FLAGS) -O3 -o shell

clean:
	rm shell
