#include <stdio.h>
#include "cmdline.h"

int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linesize;
    int status = 0;
    int i, j;
    while((linesize=getline(&line, &linecap, stdin)) > 0) {
//fprintf(stderr, "Sheel loop: Parse line...:%s.\n",line);
        struct cmd_chainlink *multiCmd;
        int numCmd = cmd_parse(line, &multiCmd);
        if (numCmd < 0) {
            fprintf(stderr, "Error parsing command\n");
            //FIXME: the last command status shall be returned, not if any parse did fail
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
            //find pipes and process chain by chain
            //FIXME: Uncommented, endless loop and error processing command, so uncommented
            /*
            for (i = 0; i < numCmd;) {
                for (j = i; multiCmd[j].next == CMD_PIPE; j++); //FIXME: If parser fails checking command ending with pipe, overflow!
                cmd_runPipe(multiCmd + j, j - i);
            }
            */
            cmd_free(multiCmd, numCmd);
        }
    }
    return status;
}
