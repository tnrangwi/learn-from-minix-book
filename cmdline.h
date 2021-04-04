#ifndef CMDLINE_H
#define CMDLINE_H
/** Header file for commmand line parser */
enum cmd_next {
    CMD_TERMINATED = 0,
    CMD_PIPE = 1,
    CMD_BGROUND = 2,
    CMD_TRUE = 3,
    CMD_FALSE = 4,
    CMD_SEP = 5,
    CMD_UNSET = 255
} ;

struct cmd_simpleCmd {
    int args;
    char **words;
    char **environ;
    enum cmd_next next;
} ;

int cmd_parse(const char *line, struct cmd_simpleCmd **commands);
const char* cmd_showNext(enum cmd_next code);
void cmd_free(struct cmd_simpleCmd *commands, int numCmds);

#endif /* CMDLINE_H */
