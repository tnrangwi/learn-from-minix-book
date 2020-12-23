/**
    FIXME: 1st word for call should not contain full path to command, but only command (currently e.g. /bin/ls instead of ls)
    FIXME: When using pipe, output of 1st command becomes input of shell, shell input is gone.
        Test with e.g. /bin/ls | /bin/grep Make - this is when the execve fails, the pipe itself seems to work
    FIXME: /bin/ls | /bin/grep Make gives "no such file or directory, as if there is an additional argument"
    FIXME: When running a pipe, there is one exit() too much, the shell exists.
    FIXME: Again think about how to free memory properly, especially when calling executing with execve.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include "cmdline.h"

#define PARSE_UNKNOWN 0
#define PARSE_WHITESPACE 1
#define PARSE_WORD 2
#define PARSE_PIPE 3
#define PARSE_BGROUND 4

extern char **environ;


/**
    Display function: return descriptive string for continuation marker.
    @param code: Code how command chain is continued.
    @type code: cmd_next (enum value from struct cmd_chainlink)
    @return: Descriptive string
    @rtype: const string (const char *)
 */
const char* cmd_showNext(enum cmd_next code) {

    switch (code) {
        case CMD_TERMINATED: return "<terminated>";
        case CMD_PIPE: return "|";
        case CMD_BGROUND: return "&";
        case CMD_TRUE: return "&&";
        case CMD_FALSE: return "||";
        case CMD_UNSET: return "<unset continuation, implementation error>";
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
static int isIn(const char c, const char *list) {
    const char *p = list;
    if (list == NULL) return 0;
    for (p = list; *p != c && *p != '\0'; p++);
    return *p != '\0';
}

/**
  Free whole struct cmd array
 */
void cmd_free(struct cmd_chainlink *commands, int numCmds) {
    int i, j;
    struct cmd_chainlink* current;
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
int cmd_parse(const char *line, struct cmd_chainlink **commands) {
    int numCmds = 0; //Length of command list
    int numChars = 0; //Length of currently parsed word
    struct cmd_chainlink *result = NULL; //array with command lists
    struct cmd_chainlink *tmpResult; //temporary helper
    struct cmd_chainlink *actCmd = NULL; //pointer to currently handled command
    char **tmpWords = NULL; //Temporary pointer for memory allocation
    char *tmpWord = NULL; //Temporary pointer for memory allocation
    //character constants
    const char *commandChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567_-@.,=/";
    const char *whiteSpace = " \t\r";
    const char *pos; //looper through command line
    const int initBufsize = 32;
    int bufsize;
    int state; //track current state of parsing
    if (commands == NULL || line == NULL) {
        errno = EINVAL;
        return -1;
    }
    pos = line;
    state = PARSE_WHITESPACE;
//fprintf(stderr, "Start parsing commandline...:%s.\n",line);
//FIXME: For use with execve, a null terminated last element is needed
//FIXME: Several similar lines of code should be put into macro
    for (pos = line; *pos != '\0' && *pos != '\n'; pos++) {
//fprintf(stderr, "Character:%c.\n", *pos);
        switch (state) {
            case PARSE_WHITESPACE:
                if (isIn(*pos, whiteSpace)) { 
                    ; //ignore only, nothing to do
                } else if (isIn(*pos, commandChars)) {
                    //either a new command line starts, or a new word in the current command line
                    if (numCmds == 0 || actCmd == NULL) { //FIXME; actCmd == NULL should be enough
                        //FIXME: use one block and realloc with NULL pointer in 1st alloc, same code
                        if (numCmds == 0) { //1st command ever
                            result = (struct cmd_chainlink *) malloc(++numCmds * sizeof(struct cmd_chainlink));
                            if (result == NULL) {
                                errno = ENOMEM;
                                return -1;
                            }
                        } else { //next command
                            tmpResult = (struct cmd_chainlink *) realloc(result, ++numCmds * sizeof(struct cmd_chainlink));
                            if (tmpResult == NULL) {
                                cmd_free(result, numCmds - 1);
                                errno = ENOMEM;
                                return -1;
                            } else {
                                result = tmpResult;
                            }
                        }
                        actCmd = result + numCmds - 1;
                        actCmd->args = 0;
                        //actCmd->words = NULL;
                    }
                    if (actCmd->args == 0) { //1st word in command
                        //FIXME: With actCmd->words initialized above just use realloc block
                        actCmd->words = (char **) malloc(++(actCmd->args) * sizeof(char *));
                        if (actCmd->words == NULL) {
                            actCmd->args--;
                            cmd_free(result, numCmds);
                            errno = ENOMEM;
                            return -1;
                        }
                    } else { //add word to command
                        tmpWords = (char **) realloc(actCmd->words, ++(actCmd->args) * sizeof(char *));
                        if (tmpWords == NULL) {
                            actCmd->args--;
                            cmd_free(result, numCmds);
                            errno = ENOMEM;
                            return -1;
                        } else {
                            actCmd->words = tmpWords;
                        }
                    }
                    //allocate memory for this word
                    actCmd->words[actCmd->args - 1] = (char *) malloc(initBufsize * sizeof(char));
                    if (actCmd->words[actCmd->args - 1] == NULL) {
                        cmd_free(result, numCmds);
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
                        cmd_free(result, numCmds);
                        fprintf(stderr, "Got | without command\n");
                        return -2;
                    }
                    state = PARSE_PIPE;
                } else if(*pos == '&') {
                    if (actCmd == NULL) {
                        cmd_free(result, numCmds);
                        fprintf(stderr, "Got & without command\n");
                        return -2;
                    }
                    state = PARSE_BGROUND;
                } else {
                    cmd_free(result, numCmds);
                    fprintf(stderr, "Got %c while parsing white space - unexpected\n", *pos);
                    return -2;
                }
                break;
            case PARSE_WORD: //parse word of command, so we have a valid command
                if (isIn(*pos, commandChars)) {
                    if (++numChars >= bufsize) {
                        bufsize += initBufsize;
                        if (bufsize <= 0) { //overflow
                            cmd_free(result, numCmds);
                            errno = ERANGE;
                            return -1;
                        }
                        tmpWord = (char *) realloc(actCmd->words[actCmd->args - 1], bufsize);
                        if (tmpWord == NULL) {
                            cmd_free(result, numCmds);
                            errno = ENOMEM;
                            return -1;
                        }
                        actCmd->words[actCmd->args -1] = tmpWord;
                    }
                    actCmd->words[actCmd->args - 1][numChars - 1] = *pos;
                } else if (isIn(*pos, whiteSpace)) {
                    //Check whether buffer has room for the terminator
                    actCmd->words[actCmd->args - 1][numChars] = '\0';
                    state = PARSE_WHITESPACE;
                }  else {
                //FIXME: This means white space before any | or & is mandatory
                    cmd_free(result, numCmds);
                    fprintf(stderr, "While parsing word, got %c.\n", *pos);
                    return -2;
                }
                break;
            case PARSE_PIPE:
                if (*pos == '|') {
                    actCmd->next = CMD_FALSE;
                    state = PARSE_WHITESPACE;
                    //FIXME: Better set this to PARSE_OR or something to make sure there will be valid chars afterwards in this state
                    actCmd = NULL;
                } else if (isIn(*pos, whiteSpace)) {
                    actCmd->next = CMD_PIPE;
                    state = PARSE_WHITESPACE;
                    actCmd = NULL;
                } else if (isIn(*pos, commandChars)) {
                    actCmd->next = CMD_PIPE;
                    //same as if new command opened after whitespace put that into function first
                    cmd_free(result, numCmds);
                    fprintf(stderr, "Pipe after command not yet supported. Use a whitespace between.\n");
                    return -2;
                    state = PARSE_WORD;
                } else {
                    cmd_free(result, numCmds);
                    fprintf(stderr, "While parsing pipe got unexpected character %c.\n", *pos);
                    return -2;
                }
                break;
            case PARSE_BGROUND:
                if (*pos == '&') {
                    actCmd->next = CMD_TRUE;
                    state = PARSE_WHITESPACE;
                    //FIXME: See above
                    actCmd = NULL;
                } else if (isIn(*pos, whiteSpace)) {
                    actCmd->next = CMD_BGROUND;
                    state = PARSE_WHITESPACE;
                    //FIXME: See above
                    actCmd = NULL;
                } else {
                    cmd_free(result, numCmds);
                    fprintf(stderr, "While parsing pipe got unexpected character %c.\n", *pos);
                    return -2;
                }
                break;
            default:
                cmd_free(result, numCmds);
                fprintf(stderr, "Unexpected parser state %d while parsing %c.\n", state, *pos);
                return -3;
        }
    }
//fprintf(stderr, "Left loop\n");
    //Parsing finished - check state to terminate command properly - if termination did not happen
    //already.
    //FIXME: Check when this is necessary, as in the last command we may get here without an act command.
    if (actCmd != NULL) {
        switch (state) {
            case PARSE_WHITESPACE:
                actCmd->next = CMD_TERMINATED;
                break;
            case PARSE_WORD:
                //FIXME: Overflow if buffer is too small. There might not be enough place here for the terminator
                actCmd->words[actCmd->args - 1][numChars] = '\0';
                actCmd->next = CMD_TERMINATED;
                break;
            case PARSE_PIPE:
                cmd_free(result, numCmds);
                fprintf(stderr, "Unexpected pipe at end of command line.\n");
                return -2;
                break;
            case PARSE_BGROUND:
                actCmd->next = CMD_BGROUND;
                break;
            default:
                cmd_free(result, numCmds);
                fprintf(stderr, "Unexpected state at end of parsing:%d\n", state);
                return -3;
        }
    }
    *commands = result;
    return numCmds;
}

/**
    Run a pipe. The last command of the pipe is always run, the command before the last is
    passed recursively with the whole rest of the pipe to be processed.
 */
int cmd_runPipe(struct cmd_chainlink *chain, int chainlength) {
    int childcall;
    int result;
fprintf(stderr, "Run pipe: chainlength =%d\n", chainlength);
    if (chainlength > 0) {
        //another sub process needed for pipe...
        int fds[2];
        int childpipe;
        if (pipe(fds) != 0) {
            perror("Creating pipe failed!");
            exit(EXIT_FAILURE);
        }
        childpipe = fork();
        if (childpipe > 0) {
            close(fds[1]); //close write end of pipe
            close(0); dup(fds[0]); //replace stdin by pipe reading end
        } else if (childpipe == 0) {
            close(fds[0]);
            close(1); dup(fds[1]);
            result = cmd_runPipe(chain - 1, chainlength - 1);
            fprintf(stderr, "Sub-pipe returned %d. Exiting sub-process\n", result);
            exit(EXIT_SUCCESS);
        } else {
            fprintf(stderr, "Fork failed in split pipe\n");
            //Either this is our main process - then return, command will fail
            //or this is a child already - then caller will terminate process
            return -1;
        }
    }
    childcall = fork();
    //FIXME: 1st search for command before forking. Has to be with (relative or absolute) path.
    if (childcall == 0) {
        //do redirections of command
        //handling of commands, search command in path, etc
        char **argv;
        argv = (char **) malloc((chain->args + 2) * sizeof(char *));
        if (argv == NULL) {
            perror("Error allocating argv");
            exit(EXIT_FAILURE);
        }
        //FIXME: The 1st word should only contain the command name, not the command name with full path
        int i_;
        for (i_=0;i_<chain->args;i_++) {
            argv[i_] = malloc((strlen(chain->words[i_]) + 1) * sizeof(char));
            if (argv[i_] == NULL) {
                perror("Error allocating argv string");
                exit(EXIT_FAILURE);
            }
            strcpy(argv[i_], chain->words[i_]);
        }
        argv[chain->args] = NULL;
        //At this point the argv clearly is for the new process.
        //How do I make sure to free allocated memory?
        execve(chain->words[0], argv, environ);
        //we are still here - unable to start command - error handling
        fprintf(stderr, "Error executing external call:%s:Error:%s\n", chain->words[0], strerror(errno));
        exit(EXIT_FAILURE);
    } else if (childcall > 0) {
        int callstat = 0;
        pid_t r = waitpid(childcall, &callstat, 0);
        if (r > 0) {
            if (WIFEXITED(callstat)) {
                return WEXITSTATUS(callstat);
            } else {
                fprintf(stderr, "Process got signal:%d\n", WTERMSIG(callstat));
                return -1;
            }
        } else {
            fprintf(stderr, "Wait for caller terminated unexpectedly\n");
            return -1;
        }
    } else {
        fprintf(stderr, "Fork failed in start job\n");
        return -1;
    }
}
