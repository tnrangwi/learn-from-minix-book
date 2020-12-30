/**
    FIXME: 1st word for call should not contain full path to command, but only command (currently e.g. /bin/ls instead of ls)
  * Error code not handled properly (-1 is used, and not made available anywhere)
  * && and || still do not work, nor does ; or &
  * No signal handling up to now
  * Tests for memory leaks / memory usage still necessary
  * Error handling when one of the commands does not exist / exec fails - is this correct currently?
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
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
    @type code: cmd_next (enum value from struct cmd_simpleCmd)
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
 Pick a given command and find out if it is with path or not.
 If without path, search command in environment variable PATH.
 If PATH is not set, default to a minimum of /bin:/usr/bin.
 If PATH is set to empty string, do not use a default PATH instead.
 This is the default behaviour, documented with Minix, checked with
 Darwin (BSD) and Solaris. Linux behaves differently, either empty
 or unset PATH does a search in the current directory. This is
 stupid behaviour.
 When found, check whether file is executable.
 @param cmd: The command string to analyse
 @type cmd: const char *
 @param name: Put shortname for this command here
 @type name: const char ** (address of a char * string)
 @param path: Put full path to this command here
 @type path: const char ** (address of a char* string)
 @return: 0 for error, else for successful
 @rtype: int
 */
static int findCommand(const char *cmd, char **name, char **path) {
    if (cmd == NULL || name == NULL || path == NULL || cmd[0] == '\0') {
        fprintf(stderr, "Internal error: Null pointer given for command or empty command:%s%s%s\n",
            cmd == NULL ? "cmd," : ",", name == NULL ? "name," : ",", path == NULL ? "path" : "");
        return 0;
    }
    if (strnlen(cmd, PATH_MAX + 1) > PATH_MAX) {
        fprintf(stderr, "Command too long or not properly terminated\n");
        return 0;
    }
    size_t cmdLen = strlen(cmd);
    char *last = strrchr(cmd, '/');
    if (last == NULL) { //internal command or command without path
        *name = (char *) malloc((cmdLen + 1) * sizeof(char));
        if (*name == NULL) {
            perror("Cannot allocate memory for command name");
            return 0;
        }
        strcpy(*name, cmd);
        const char *searchPath = getenv("PATH");
        if (searchPath == NULL) searchPath = "/bin:/usr/bin";
        const char *start = searchPath;
        const char *end;
        size_t pathLen = strlen(searchPath);
        *path = NULL;
        //FIXME: I can read that quite well, the value of ? is not used, but || generates integer and assignment generates pointer
        //This warning can be removed using nested ? operators.
        for (start = searchPath, (end = strchr(start, ':')) || (end = pathLen > 0 ? searchPath + pathLen : NULL);
                end != NULL;
                start = end + 1, start <= searchPath + pathLen ? (end = strchr(start, ':')) || (end = searchPath + pathLen) : (end = NULL)) {
            if (start == end) { //Special case, empty path component means current directory
                *path = (char *) malloc((cmdLen + 3) * sizeof(char));
                if (*path == NULL) {
                    perror("Cannot allocate memory for search path");
                    free(*name);
                    return 0;
                }
                strcpy(*path, "./");
                strcpy(*path + 2, cmd);
            } else {
                size_t thisPathLen = end - start;
                //guide against overflow, check twice
                if (thisPathLen > PATH_MAX || thisPathLen + cmdLen + 2 > PATH_MAX) {
                    //FIXME: Debug only output that we skip this
                    continue;
                }
                *path = (char *) malloc((thisPathLen + strlen(cmd) + 2) * sizeof(char));
                if (*path == NULL) {
                    perror("Cannot allocate memory for search path");
                    free(*name);
                    return 0;
                }
                strncpy(*path, start, thisPathLen);
                strcpy(*path + thisPathLen, "/");
                strcpy(*path + thisPathLen + 1, cmd);
            }
            if (access(*path, F_OK) == 0) {
                break;
            } else {
                free(*path);
                *path = NULL;
            }
        }
        if (*path == NULL) free(*name);
    } else {
        if (last[1] == '\0') {
            fprintf(stderr, "Empty command:%s\n", cmd);
            return 0;
        }
        *name = (char *) malloc((strlen(last + 1) + 1) * sizeof(char));
        if (*name == NULL) {
            perror("Cannot allocate memory for command name");
            return 0;
        }
        strcpy(*name, last + 1);
        *path = (char *) malloc((strlen(cmd) + 1) * sizeof(char));
        if (*path == NULL) {
            perror("Cannot allocate memory for command path");
            free(*name);
            return 0;
        }
        strcpy(*path, cmd);
    }
    if (*path == NULL) return 0;
    if (access(*path, X_OK) == 0) {
        return 1;
    } else {
        free(*name);
        free(*path);
        return 0;
    }
}

/**
  Free whole struct cmd array
 */
void cmd_free(struct cmd_simpleCmd *commands, int numCmds) {
    int i, j;
    struct cmd_simpleCmd* current;
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
int cmd_parse(const char *line, struct cmd_simpleCmd **commands) {
    int numCmds = 0; //Length of command list
    int numChars = 0; //Length of currently parsed word
    struct cmd_simpleCmd *result = NULL; //array with command lists
    struct cmd_simpleCmd *tmpResult; //temporary helper
    struct cmd_simpleCmd *actCmd = NULL; //pointer to currently handled command
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
                            result = (struct cmd_simpleCmd *) malloc(++numCmds * sizeof(struct cmd_simpleCmd));
                            if (result == NULL) {
                                errno = ENOMEM;
                                return -1;
                            }
                        } else { //next command
                            tmpResult = (struct cmd_simpleCmd *) realloc(result, ++numCmds * sizeof(struct cmd_simpleCmd));
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
    Run a pipe or a simple command.
 */
int cmd_runPipe(struct cmd_simpleCmd *commands, int maxCmd) {
    int nCmd;
    int fds[2];
    int thisIn=-1, thisOut=-1, nextIn=-1;
    int childpid;
    int i;

fprintf(stderr, "Run pipe, number of simple commands:%d\n", maxCmd + 1);
    for (nCmd = 0; nCmd <= maxCmd; nCmd++) {
//fprintf(stderr, "Command %d:'%s'\n", nCmd, commands[nCmd].words[0]);
        if (nCmd < maxCmd) {
            if (pipe(fds) != 0) {
                perror("Creating pipe failed");
                //FIXME: Do not exit the shell
                exit(EXIT_FAILURE);
            }
//fprintf(stderr, "Pipe create, read fd=%d, write fd=%d\n", fds[0], fds[1]);
            nextIn = fds[0];
            thisOut = fds[1];
        } else {
            thisOut = -1;
            nextIn = -1;
        }
        //maybe we should search for the command before forking at all
        if ((childpid = fork()) == 0) {
            if (nextIn >= 0) {
                close(nextIn);
            }
            if (thisIn >= 0) {
                close(0);
                dup(thisIn);
            }
            if (thisOut >= 0) {
                close(1);
                dup(thisOut);
            }
            //do further redirections of command, check case where we redirect before piping (e.g. 2>&1 | less)
            //handling of commands, search command in path, etc
            char **argv;
            argv = (char **) malloc((commands[nCmd].args + 2) * sizeof(char *));
            if (argv == NULL) {
                perror("Error allocating argv");
                exit(EXIT_FAILURE);
            }
            char *cmdName, *cmdPath;
            if (!findCommand(commands[nCmd].words[0], &cmdName, &cmdPath)) {
                fprintf(stderr, "Cannot find command:%s\n", commands[nCmd].words[0]);
                exit(EXIT_FAILURE);
            }
            argv[0] = cmdName;
            for (i=1;i<commands[nCmd].args;i++) {
                argv[i] = malloc((strlen(commands[nCmd].words[i]) + 1) * sizeof(char));
                if (argv[i] == NULL) {
                    perror("Error allocating argv string");
                    exit(EXIT_FAILURE);
                }
                strcpy(argv[i], commands[nCmd].words[i]);
            }
            argv[commands[nCmd].args] = NULL;
            execve(cmdPath, argv, environ);
            //we are still here - unable to start command - error handling
            fprintf(stderr, "Error executing external call:%s:Error:%s\n", commands[nCmd].words[0], strerror(errno));
            exit(EXIT_FAILURE);
        } else if (childpid > 0) {
            //FIXME: Maintain some information about this process so we can lookup it later
            if (thisOut >= 0) {
//fprintf(stderr, "Close fd %d in shell\n", thisOut);
                close(thisOut);
            }
            if (thisIn >= 0) {
//fprintf(stderr, "Close fd %d in shell\n", thisIn);
                close(thisIn);
            }
            thisIn = nextIn;
        } else {
            perror("Cannot fork");
            //FIXME: We may already have started a processes. What shall we do in this case?
            //Yes, leave the loop for sure. Further on we should close all file descriptors we have created with pipe()
            return -1;
        }

    }
//fprintf(stderr, "Wait for last command to terminate\n");
    int callstat = 0;
    pid_t r = waitpid(childpid, &callstat, 0);
//fprintf(stderr, "Last command exited\n");
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

}
