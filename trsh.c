#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

enum nextCmd_t {
    CONT_TERMINATED = 0,
    CONT_PIPE = 1,
    CONT_BGROUND = 2,
    CONT_TRUE = 3,
    CONT_FALSE = 4,
    CONT_UNSET = 255
    
} ;

enum parseState {
    PARSE_UNKNOWN = 0,
    PARSE_WHITESPACE = 1,
    PARSE_WORD = 2,
    PARSE_PIPE = 3,
    PARSE_BGROUND = 4
} ;



struct cmd {
    int args;
    char **words;
    enum nextCmd_t next;
} ;

const char* showNextCmd(enum nextCmd_t code) {

    switch (code) {
        case CONT_TERMINATED: return "";
        case CONT_PIPE: return "|";
        case CONT_BGROUND: return "&";
        case CONT_TRUE: return "&&";
        case CONT_FALSE: return "||";
        case CONT_UNSET: return "<unset continuation, implementation error>";
        default:
            fprintf(stderr, "Received unimplemented continuation code:%d - implementation error\n", code);
            return "<Implementation error, unknown continuation code>";
    }

}


/**
 Match character against list of characters.
 @param c: The character to check
 @type c: const char
 @param list: zero terminated array of characters
 @type list: const char *
 @return: 0 for false, else for true
 @rtype: int
 */
int isIn(const char c, const char *list) {
    const char *p = list;
    if (list == NULL) return 0;
    for (p = list; *p != c && *p != '\0'; p++);
    return *p != '\0';
}

/**
  Free whole struct cmd array
 */
void freeCmd(struct cmd *commands, int numCmds) {
    int i, j;
    struct cmd* current;
    for (i=0, current = commands; i< numCmds; i++, current++) {
        //fprintf(stderr, "Free command %d\n", i);
        for (j=0; j < current->args; j++) {
            //fprintf(stderr, "Free word %d\n", j);
            free(current->words[j]);
        }
        //fprintf(stderr, "Free words.\n");
        free(current->words);
    }
    //fprintf(stderr, "Free command itself\n");
    free(commands);
}

/**
 Split up the command line into a list of command structures.
 @param line: Command line to parse, terminated by zero, may optionally contain a trailing newline.
 @type line: const char *
 @param cmd: pointer set to array of command structures
 @rype cmd: struct cmd **: *cmd is array of cmd structures
 @return: Number of parsed command sequences. If line is empty, then 0.
 @rtype: int
 */
int parseCmdLine(const char *line, struct cmd **commands) {
    int numCmds = 0; //Length of command list
    int numChars = 0; //Length of currently parsed word
    struct cmd *result = NULL; //array with command lists
    struct cmd *tmpResult; //temporary helper
    struct cmd *actCmd = NULL; //pointer to currently handled command
    char **tmpWords = NULL; //Temporary pointer for memory allocation
    char *tmpWord = NULL; //Temporary pointer for memory allocation
    //character constants
    const char *commandChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567_-@.,=";
    const char *whiteSpace = " \t\r";
    const char *pos; //looper through command line
    const int initBufsize = 32;
    int bufsize;
    enum parseState state; //track current state of parsing
    if (commands == NULL || line == NULL) {
        errno = EINVAL;
        return -1;
    }
    pos = line;
    state = PARSE_WHITESPACE;
    for (pos = line; *pos != '\0' && *pos != '\n'; pos++) {
        switch (state) {
            case PARSE_WHITESPACE:
                if (isIn(*pos, whiteSpace)) { 
                    ; //ignopre only, nothing to do
                } else if (isIn(*pos, commandChars)) {
                    //either a new command line starts, or a new word in the current command line
                    if (numCmds == 0 || actCmd == NULL) {
                        if (numCmds == 0) { //1st command ever
                            result = (struct cmd *) malloc(++numCmds * sizeof(struct cmd));
                            if (result == NULL) {
                                errno = ENOMEM;
                                return -1;
                            }
                        } else { //next command
                            tmpResult = (struct cmd *) realloc(result, ++numCmds * sizeof(struct cmd));
                            if (tmpResult == NULL) {
                                freeCmd(result, numCmds - 1);
                                errno = ENOMEM;
                                return -1;
                            } else {
                                result = tmpResult;
                            }
                        }
                        actCmd = result + numCmds - 1;
                        actCmd->args = 0;
                    }
                    if (actCmd->args == 0) { //1st word in command
                        actCmd->words = (char **) malloc(++(actCmd->args) * sizeof(char *));
                        if (actCmd->words == NULL) {
                            actCmd->args--;
                            freeCmd(result, numCmds);
                            errno = ENOMEM;
                            return -1;
                        }
                    } else { //add word to command
                        tmpWords = (char **) realloc(actCmd->words, ++(actCmd->args) * sizeof(char *));
                        if (tmpWords == NULL) {
                            actCmd->args--;
                            freeCmd(result, numCmds);
                            errno = ENOMEM;
                            return -1;
                        } else {
                            actCmd->words = tmpWords;
                        }
                    }
                    //allocate memory for this word
                    actCmd->words[actCmd->args - 1] = (char *) malloc(initBufsize * sizeof(char));
                    if (actCmd->words[actCmd->args - 1] == NULL) {
                        freeCmd(result, numCmds);
                        errno = ENOMEM;
                        return -1;
                    }
                    numChars = 1;
                    bufsize = initBufsize;
                    //Finally, copy the single character into the command word :-)
                    actCmd->words[actCmd->args - 1][numChars - 1] = *pos;
                    state = PARSE_WORD;
                } else if(*pos == '|') {
                    if (actCmd == NULL) {
                        freeCmd(result, numCmds);
                        fprintf(stderr, "Got | without command\n");
                        return -2;
                    }
                    state = PARSE_PIPE;
                } else if(*pos == '&') {
                    if (actCmd == NULL) {
                        freeCmd(result, numCmds);
                        fprintf(stderr, "Got & without command\n");
                        return -2;
                    }
                    state = PARSE_BGROUND;
                } else {
                    freeCmd(result, numCmds);
                    fprintf(stderr, "Got %c while parsing white space - unexpected\n", *pos);
                    return -2;
                }
                break;
            case PARSE_WORD: //parse word of command, so we have a valid command
                if (isIn(*pos, commandChars)) {
                    if (++numChars >= bufsize) {
                        bufsize += initBufsize;
                        if (bufsize <= 0) { //overflow
                            freeCmd(result, numCmds);
                            errno = ERANGE;
                            return -1;
                        }
                        tmpWord = (char *) realloc(actCmd->words[actCmd->args - 1], bufsize);
                        if (tmpWord == NULL) {
                            freeCmd(result, numCmds);
                            errno = ENOMEM;
                            return -1;
                        }
                        actCmd->words[actCmd->args -1] = tmpWord;
                    }
                    actCmd->words[actCmd->args - 1][numChars - 1] = *pos;
                } else if (isIn(*pos, whiteSpace)) {
                    actCmd->words[actCmd->args - 1][numChars] = '\0';
                    state = PARSE_WHITESPACE;
                }  else {
                    freeCmd(result, numCmds);
                    fprintf(stderr, "While parsing word, got %c.\n", *pos);
                    return -2;
                }
                break;
            case PARSE_PIPE:
                if (*pos == '|') {
                    actCmd->next = CONT_FALSE;
                    state = PARSE_WHITESPACE;
                    actCmd = NULL;
                } else if (isIn(*pos, whiteSpace)) {
                    actCmd->next = CONT_PIPE;
                    state = PARSE_WHITESPACE;
                    actCmd = NULL;
                } else if (isIn(*pos, commandChars)) {
                    actCmd->next = CONT_PIPE;
                    //same as if new command opened after whitespace put that into function first
                    freeCmd(result, numCmds);
                    fprintf(stderr, "Pipe after command not yet supported. Use a whitespace between.\n");
                    return -2;
                    state = PARSE_WORD;
                } else {
                    freeCmd(result, numCmds);
                    fprintf(stderr, "While parsing pipe got unexpected character %c.\n", *pos);
                    return -2;
                }
                break;
            case PARSE_BGROUND:
                if (*pos == '&') {
                    actCmd->next = CONT_TRUE;
                    state = PARSE_WHITESPACE;
                    actCmd = NULL;
                } else if (isIn(*pos, whiteSpace)) {
                    actCmd->next = CONT_BGROUND;
                    state = PARSE_WHITESPACE;
                    actCmd = NULL;
                } else {
                    freeCmd(result, numCmds);
                    fprintf(stderr, "While parsing pipe got unexpected character %c.\n", *pos);
                    return -2;
                }
                break;
            default:
                freeCmd(result, numCmds);
                fprintf(stderr, "Unexpected parser state %d while parsing %c.\n", state, *pos);
                return -3;
        }
    }
    switch (state) {
        case PARSE_WHITESPACE:
            actCmd->next = CONT_TERMINATED;
            break;
        case PARSE_WORD:
            actCmd->words[actCmd->args - 1][numChars] = '\0';
            actCmd->next = CONT_TERMINATED;
            break;
        case PARSE_PIPE:
            freeCmd(result, numCmds);
            fprintf(stderr, "Unexpected pipe at end of command line.\n");
            return -2;
            break;
        case PARSE_BGROUND:
            actCmd->next = CONT_BGROUND;
            break;
        default:
            freeCmd(result, numCmds);
            fprintf(stderr, "Unexpected state at end of parsing:%d\n", state);
            return -3;
    }
    *commands = result;
    return numCmds;
}

int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linesize;
    int i, j;
    while((linesize=getline(&line, &linecap, stdin)) > 0) {
        struct cmd *multiCmd;
        int numCmd = parseCmdLine(line, &multiCmd);
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
                fprintf(stderr, "Continued with:%s\n", showNextCmd(multiCmd[i].next));
            }
            freeCmd(multiCmd, numCmd);
        }
    }
    return 0;
}
