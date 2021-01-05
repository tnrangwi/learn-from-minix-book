#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "cmdline.h"
#include "trsh.h"

struct trsh_stat trsh_status;

static void sigHandler(int sig) {
    //FIXME: This should do something: Wait for all currently running processes terminating
    fprintf(stdout, "\n");
}

static void init(int argc, char *argv[]) {
    int i;
    trsh_status.interactive = isatty(fileno(stdin)) && isatty(fileno(stdout));
    errno = 0; //Clear eventually set ENOTTY
    trsh_status.prog = argv[0];
    while ((i = getopt(argc, argv, ":")) != -1) {
        switch (i) {
            default: ; //Ignore all options for now
        }
    }
    argc -= optind;
    argv += optind;

    if (argc > 0) {
        trsh_status.interactive = 0;
        trsh_status.script = argv[0];
        if (freopen(trsh_status.script, "r", stdin) == NULL) {
            fprintf(stderr, "Cannot open file '%s':%s", trsh_status.script, strerror(errno));
            _exit(1);
        }
        //FIXME: Now set positional parameters from the remaining arguments
    }
    if (trsh_status.interactive) {
        trsh_status.sigINT.sa_flags = 0;
        trsh_status.sigINT.sa_mask = 0;
        trsh_status.sigINT.sa_handler = sigHandler;
        if (sigaction(SIGINT, &trsh_status.sigINT, &trsh_status.sigINTsave) != 0) {
            perror("Cannot install initial signal handler (SIGINT)");
            _exit(1);
        }
        if (sigaction(SIGQUIT, &trsh_status.sigINT, &trsh_status.sigQUITsave) != 0) {
            perror("Cannot install initial signal handler (SIGQUIT)");
            _exit(1);
        }
    }
}


int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linesize;
    int status = 0;
    int i, j;
    init(argc, argv);
    for(;;) {
        if (trsh_status.interactive) fputs("$ ", stdout);
        linesize=getline(&line, &linecap, stdin);
        if (linesize < 0) {
            if(errno == EINTR) { //Interrupted by system call, e.g. user did press <CTRL>-C
                errno = 0;       //reset, errno is set on error only. Otherwise EOF from getline is interpreted as interrupt.
                continue;
            } else if (errno == 0) { //EOF indicated by getline()
                break;
            } else {
                perror("Error reading commands");
                _exit(1);
            }
        } else if (linesize == 0) { //This is almost impossible as getline waits for at least one line. Switching to getch may need this.
            break;
        }
//fprintf(stderr, "Sheel loop: Parse line...:%s.\n",line)
        struct cmd_simpleCmd *multiCmd;
        int numCmd = cmd_parse(line, &multiCmd);
        if (numCmd < 0) {
            fprintf(stderr, "Error parsing command\n");
            status = 1;
        } else if (numCmd == 0) {
//fprintf(stderr, "Ignore empty line\n");
            continue;
        } else {
//fprintf(stderr, "Num commands:%d\n", numCmd);
            for (i=0; i < numCmd;i++) {
                fprintf(stderr, "Command:%s, words%d\n", multiCmd[i].words[0], multiCmd[i].args);
                for (j=1; j < multiCmd[i].args; j++) {
                    fprintf(stderr, "arg %d:%s.\n", j, multiCmd[i].words[j]);
                }
                fprintf(stderr, "Continued with:%s\n", cmd_showNext(multiCmd[i].next));
            }
//fprintf(stderr, "Free....%d\n",numCmd);
            //find pipes and process pipe by pipe
            for (i = 0; i < numCmd;) {
                //Find end of this list of commands. Pipes must be run in once, connected.
                //When command is not terminated with pipe, then we run it up to exactly this position.
                //j is the position of the last command to be run together with this command
                for (j = i; multiCmd[j].next == CMD_PIPE; j++); //FIXME: If parser fails checking command ending with pipe, overflow!
                status = cmd_runPipe(multiCmd + i, j - i);
fprintf(stderr, "Return code:%d\n", status);
                //FIXME: Now base decision for next command on return code
                i = j + 1;
            }
            cmd_free(multiCmd, numCmd);
        }
    }
    return status;
}
