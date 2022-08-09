CC=cc
CFLAGS=-O3 -pipe -Wall
LIBS=-pthread

main: src/main.c
	$(CC) $(CFLAGS) -o $@ $(LIBS) $<
