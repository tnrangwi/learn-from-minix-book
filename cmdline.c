/**
  FIXME
  * & does not work yet
  * Signal handling incomplete. Should handle SIGHUP properly, handle SIGCHLD to maintain process list, SIGSTP while running process.
  * Implement fg / bg for job control. Goes hand in hand with changes in signal handling.
  * Implement at least ``
  * Implement redirections
  * Implement shell variables and export, especially setting variables for external calls
  * Tests for memory leaks / memory usage still necessary
  * Error handling when one of the commands does not exist / exec fails - is this correct currently?
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "cmdline.h"
#include "log.h"

#define PARSE_UNKNOWN 0
#define PARSE_WHITESPACE 1
#define PARSE_WORD 2
#define PARSE_PIPE 3
#define PARSE_BGROUND 4
#define PARSE_VAR 5


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
        case CMD_SEP: return ";";
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
void cmd_free(struct cmd_simpleCmd *commands, int numCmds) {
    int i, j;
    struct cmd_simpleCmd* current;
    for (i=0, current = commands; i< numCmds; i++, current++) {
        log_out(2, "Free command %d\n", i);
        for (j=0; j < current->args; j++) {
            log_out(2, "Free word %d\n", j);
            free(current->words[j]);
        }
        log_out(2, "Free words.\n");
        free(current->words);
        if (current->environ) {
            for (j=0; current->environ[j] != NULL; j++) {
                free(current->environ[j]);
            }
            free(current->environ);
        }
    }
    log_out(2, "Free command itself\n");
    free(commands);
}

static struct cmd_simpleCmd *cmd_alloc(struct cmd_simpleCmd *commands, int numCmds) {
    struct cmd_simpleCmd *result;
    if (numCmds <= 0 || numCmds > 1 && commands == NULL) {
        log_out(0, "Internal error in cmd_alloc: Error in command pipeline:%d/%p", numCmds, commands);
        return NULL;
    }
    if (numCmds == 1) { //1st command ever
        result = (struct cmd_simpleCmd *) malloc(numCmds * sizeof(struct cmd_simpleCmd));
    } else { //next command
        result = (struct cmd_simpleCmd *) realloc(commands, numCmds * sizeof(struct cmd_simpleCmd));
        if (result == NULL) {
            cmd_free(commands, numCmds - 1);
        }
    }
    if (result == NULL) {
        log_out(0, "Error allocating memory for command line");
        return NULL;
    }
    struct cmd_simpleCmd *last = result + numCmds - 1;
    last->args = 0;
    last->words = NULL;
    last->environ = NULL;
    return result;
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
    struct cmd_simpleCmd *actCmd = NULL; //pointer to currently handled command
    char **tmpWords = NULL; //Temporary pointer for memory allocation
    char *tmpWord = NULL; //Temporary pointer for memory allocation
    //character constants
    const char *commandChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-@.,=/#:";
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
    log_out(2, "Start parsing commandline...:%s.\n",line);
//FIXME: For use with execve, a null terminated last element is needed
//FIXME: Several similar lines of code should be put into macro
    for (pos = line; *pos != '\0' && *pos != '\n'; pos++) {
        log_out(2, "Character:%c.\n", *pos);
        switch (state) {
            case PARSE_WHITESPACE:
                if (isIn(*pos, whiteSpace)) {
                    ; //ignore only, nothing to do
                } else if (actCmd == NULL &&  *pos == '#') {
                    //FIXME: Rather incomplete handling of comments, only works in 1st command
                    //That's just enough to ignore comment lines and especially a she-bang line for scripts.
                    if (numCmds > 0) {
                        cmd_free(result, numCmds);
                    }
                    return 0;
                } else if (isIn(*pos, commandChars)) {
                //either a new command line starts, or a new word in the current command line
                    if (actCmd == NULL) {
                        if ((result = cmd_alloc(result, ++numCmds)) == NULL) return -1;
                        actCmd = result + numCmds - 1;
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
                } else if(*pos == ';') {
                    if (actCmd == NULL) {
                        cmd_free(result, numCmds);
                        fprintf(stderr, "Got ; without command\n");
                        return -2;
                    }
                    actCmd->next = CMD_SEP;
                    state = PARSE_WHITESPACE;
                    actCmd = NULL;
                } else {
                    cmd_free(result, numCmds);
                    fprintf(stderr, "Got %c while parsing white space - unexpected\n", *pos);
                    return -2;
                }
                break;
            case PARSE_WORD: //parse word of command, so we have a valid command
                /* FIMXE: Implementation of environment parsing starts here
                if (*pos == '=' && actCmd->args == 0 || isVar(actCmd->words[0], numChars)) {
                    //Move command word to variable word
                    state = PARSE_VAR;
                } else */
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
    log_out(2, "Left parsing loop\n");
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

