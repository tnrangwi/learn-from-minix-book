#include <stdio.h>
#include "cmdline.h"

int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linesize;
    int i, j;
    while((linesize=getline(&line, &linecap, stdin)) > 0) {
        struct cmd_chainlink *multiCmd;
        int numCmd = cmd_parse(line, &multiCmd);
        if (numCmd < 0) {
            fprintf(stderr, "Error parsing command\n");
            return 1;
        } else if (numCmd == 0) {
            fprintf(stderr, "Ignore empty line\n");
            continue;
        } else {
            fprintf(stderr, "Num commands:%d\n", numCmd);
            for (i=0; i < numCmd;i++) {
                fprintf(stderr, "Command:%s, words%d\n", multiCmd[i].words[0], multiCmd[i].args);
                for (j=1; j < multiCmd[i].args; j++) {
                    fprintf(stderr, "arg %d:%s.\n", j, multiCmd[i].words[j]);
                }
                fprintf(stderr, "Continued with:%s\n", cmd_showNext(multiCmd[i].next));
            }
            cmd_free(multiCmd, numCmd);
        }
    }
    return 0;
}
