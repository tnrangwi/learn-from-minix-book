#ifndef TRSH_H
#define TRSH_H

#include <signal.h>

struct trsh_stat {
    int interactive; // whether shell is interactive
    struct sigaction sigINTsave; // original signal handler for SIGINT
    struct sigaction sigINT; // Signal handler of the shell
} ;
#endif /* TRSH_H */
