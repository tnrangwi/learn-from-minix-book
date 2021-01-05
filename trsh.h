#ifndef TRSH_H
#define TRSH_H

#include <signal.h>

struct trsh_stat {
    int interactive; // whether shell is interactive
    const char *prog; //argv[0]
    const char *script; //If arguments given - the name of the script we execute
    struct sigaction sigINTsave; // original signal handler for SIGINT
    struct sigaction sigQUITsave; // original signal handler for SIGQUIT
    struct sigaction sigINT; // Signal handler of the shell
} ;
#endif /* TRSH_H */
