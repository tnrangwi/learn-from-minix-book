.SUFFIXES:
.SUFFIXES: .c .o
.PHONY:clean

all: trsh

CC=cc
CFLAGS=-Wall -Wno-parentheses

OBJ = trsh.o cmdline.o log.o env.o

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

trsh: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)
        
clean:
	rm -f *.o

mrproper: clean
	rm -f trsh
