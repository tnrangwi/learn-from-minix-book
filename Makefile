.SUFFIXES:
.PHONY:clean

all: trsh

ODIR=obj
CC=gcc
CFLAGS=-Wall

_OBJ = trsh.o cmdline.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

trsh: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^
        
clean:
	rm -f $(ODIR)/*.o
