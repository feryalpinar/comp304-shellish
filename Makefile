CC=gcc
CFLAGS=-Wall

all:
	$(CC) $(CFLAGS) -o shell-ish shell-ish.c

clean:
	rm -f shell-ish
