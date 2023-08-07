CC=gcc
CFLAGS=-g3 -I.
DEPS = toypool.h
OBJ = toypool.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

toypool: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o *~ core toypool