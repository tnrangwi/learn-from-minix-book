.SUFFIXES:
.SUFFIXES: .c .o
.PHONY:clean
#DBG=-D MEM_DEBUG

all: trsh

CC=cc
CFLAGS=-Wall -Wno-parentheses

OBJ = trsh.o cmdline.o log.o env.o

.c.o:
	$(CC) $(CFLAGS) -c $(DBG) -o $@ $<

trsh: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)
        
clean:
	rm -f *.o

mrproper: clean
	rm -f trsh
