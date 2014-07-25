gcc -o cmdline.o -c cmdline.c &&
    gcc -o trsh.o -c trsh.c && 
    gcc -o trsh trsh.o cmdline.o
