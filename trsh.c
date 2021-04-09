#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include "cmdline.h"
#include "trsh.h"
#include "log.h"
#include "env.h"

#define RUN_ALWAYS 0
#define RUN_IF_OK 1
#define RUN_IF_ERROR 2

struct trsh_stat trsh_status;

extern char **environ;

static void sigHandler(int sig) {
    //FIXME: This should do something: Wait for all currently running processes terminating
    fprintf(stdout, "\n");
}

static int iCd(char *[]);
static int iExport(char *[]);
static int iNop(char*[]);
static int iExit(char*[]);
static int iSet(char*[]);
const char *shlFunc[] = { ":", "cd", "exit", "export", "set" };

static int (*shlCall[])(char *[]) = {
    iNop,
    iCd,
    iExit,
    iExport,
    iSet
};

static int iNop(char *argv[]) { return 0; }

static int iCd(char *argv[]) {
    const char *dir;
    if (argv == NULL || argv[0] == NULL) {
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "HOME is not set\n");
            return 1;
        }
    } else {
        dir = argv[0];
    }
    if (chdir(dir) == 0) {
        return 0;
    }
    fprintf(stderr, "Cannot chdir to %s:%s\n", dir, strerror(errno));
    return 1;
}

static int iExit(char *argv[]) {
    unsigned long rc;
    char *c;
    if (argv && argv[0]) {
        rc = 0;
        for (c = argv[0]; *c; c++) {
            if (rc * 10 < rc) {
                log_out(0, "Number out of range:%s\n", argv[0]);
                return 1;
            }
            rc *= 10;
            if (*c < '0' || *c > '9') {
                log_out(0, "Illegal number:'%s'\n", argv[0]);
                return 1;
            }
            int add = *c - '0';
            if (add + rc < rc || (rc += add) > LONG_MAX) {
                log_out(0, "Number out of range:%s\n", argv[0]);
                return 1;
            }
        }
    } else {
        rc = trsh_status.status;
    }
    _exit(rc & 255);
}

static int iExport(char *argv[]) {
    return 0;
}

static int iSet(char *argv[]) {
    env_dump();
    return 0;
}

static int srchString(char **s1, char **s2) { return strcmp(*s1, *s2); }

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
 @param name: Put shortname for this command here or NULL if command is internal.
 @type name: const char ** (address of a char * string)
 @param path: Put full path to this command here or NULL if it is an internal command
 @type path: const char ** (address of a char* string)
 @return: greater or equal zero if OK, negative on error. Number of the internal command on internal command.
 @rtype: int
 */
static int findCommand(const char *cmd, char **name, char **path) {
    if (cmd == NULL || name == NULL || path == NULL || cmd[0] == '\0') {
        fprintf(stderr, "Internal error: Null pointer given for command or empty command:%s%s%s\n",
            cmd == NULL ? "cmd," : ",", name == NULL ? "name," : ",", path == NULL ? "path" : "");
        return -1;
    }
    if (strnlen(cmd, PATH_MAX + 1) > PATH_MAX) {
        fprintf(stderr, "Command too long or not properly terminated\n");
        return -1;
    }
    size_t cmdLen = strlen(cmd);
    char *last = strrchr(cmd, '/');
    if (last == NULL) { //internal command or command without path
        const char **intCmd = (const char **) bsearch((void *) &cmd, (void *) shlFunc, sizeof(shlFunc) / sizeof(shlFunc[0]),
                                sizeof(char *), (int (*)(const void *, const void *)) srchString);
        if (intCmd != NULL) {
            *name = NULL;
            *path = NULL;
            return (int) (intCmd - shlFunc);
        }
        *name = (char *) malloc((cmdLen + 1) * sizeof(char));
        if (*name == NULL) {
            perror("Cannot allocate memory for command name");
            return -1;
        }
        strcpy(*name, cmd);
        const char *searchPath = getenv("PATH");
        if (searchPath == NULL) searchPath = "/bin:/usr/bin";
        const char *start = searchPath;
        const char *end;
        size_t pathLen = strlen(searchPath);
        *path = NULL;
        for (start = searchPath, (end = strchr(start, ':')) || (end = pathLen > 0 ? searchPath + pathLen : NULL);
                end != NULL;
                //? operator used for side effects only, cast to (int) to make compiler happy
                start=end+1, start <= searchPath+pathLen ? (end=strchr(start, ':')) || (end = searchPath+pathLen) : (int) (end=NULL)) {
            if (start == end) { //Special case, empty path component means current directory
                *path = (char *) malloc((cmdLen + 3) * sizeof(char));
                if (*path == NULL) {
                    perror("Cannot allocate memory for search path");
                    free(*name);
                    return -1;
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
                    return -1;
                }
                strncpy(*path, start, thisPathLen);
                strcpy(*path + thisPathLen, "/");
                strcpy(*path + thisPathLen + 1, cmd);
            }
            if (access(*path, F_OK) == 0) {
                break;
            } else {
                //FIXME: This in principle only is OK for file not found. We may check errno here.
                free(*path);
                *path = NULL;
                errno = 0;
            }
        }
        if (*path == NULL) free(*name);
    } else {
        if (last[1] == '\0') {
            fprintf(stderr, "Empty command:%s\n", cmd);
            return -1;
        }
        *name = (char *) malloc((strlen(last + 1) + 1) * sizeof(char));
        if (*name == NULL) {
            perror("Cannot allocate memory for command name");
            return -1;
        }
        strcpy(*name, last + 1);
        *path = (char *) malloc((strlen(cmd) + 1) * sizeof(char));
        if (*path == NULL) {
            perror("Cannot allocate memory for command path");
            free(*name);
            return -1;
        }
        strcpy(*path, cmd);
    }
    if (*path == NULL) return -1;
    if (access(*path, X_OK) == 0) {
        return INT_MAX;
    } else {
        free(*name);
        free(*path);
        return -1;
    }
}

/**
    Run a pipe or a simple command.
 */
static int runPipe(struct cmd_simpleCmd *commands, int maxCmd) {
    int nCmd;
    int fds[2];
    int thisIn=-1, thisOut=-1, nextIn=-1;
    int childpid;

    log_out(1, "Run pipe, number of simple commands:%d\n", maxCmd + 1);
    for (nCmd = 0; nCmd <= maxCmd; nCmd++) {
        log_out(1, "Command %d:'%s'\n", nCmd, commands[nCmd].words != NULL ? commands[nCmd].words[0] : "<empty>");
        char *cmdName = NULL, *cmdPath = NULL;
        int cmdCode;
        if (maxCmd == 0 && commands[nCmd].words == NULL) { //no command, only environment
            if (commands[nCmd].environ) {
                char **p;
                for(p = commands[nCmd].environ; *p; p++) {
                    env_put(*p);
                }
            }
            return 0;
        }
        if (commands[nCmd].words == NULL) { //only environment, in the middle of a pipe, useless
            cmdCode = 0; //Nop
        } else {
            cmdCode = findCommand(commands[nCmd].words[0], &cmdName, &cmdPath);
        }
        if (maxCmd == 0 && cmdCode >= 0 && cmdCode < INT_MAX) { //Exactly one internal command
            //FIXME: Set environment for call - is this really a use case?
            return (*shlCall[cmdCode])(commands[0].words + 1);
        }
        if (nCmd < maxCmd) {
            if (pipe(fds) != 0) {
                perror("Creating pipe failed");
                //FIXME: Do not exit the shell
                exit(EXIT_FAILURE);
            }
            log_out(3, "Pipe create, read fd=%d, write fd=%d\n", fds[0], fds[1]);
            nextIn = fds[0];
            thisOut = fds[1];
        } else {
            thisOut = -1;
            nextIn = -1;
        }
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
            if (cmdCode < 0) {
                fprintf(stderr, "Cannot find command:%s\n", commands[nCmd].words[0]);
                exit(EXIT_FAILURE);
            } else if (cmdCode != INT_MAX) {
                //FIXME: Internal command is safe in child here. Just execute and exit with return code.
                //Maybe not all of them. Curious what fg would do in a child process.
                //Catch especially case where we set this to Nop above and do not have arguments.
                fprintf(stderr, "Internal command not supported in pipe yet\n");
                exit(EXIT_FAILURE);
            }  else if (cmdName == NULL || cmdPath == NULL) {
                fprintf(stderr, "Internal error - one of the command pointers is NULL\n");
                exit(EXIT_FAILURE);
            }
            if (commands[nCmd].environ) {
                char **p = commands[nCmd].environ;
                while (*p != NULL) {
                    if(putenv(*p)) {
                        log_out(0, "Error setting environment:%s\n", *p);
                    }
                    p++;
                }
            }
            execve(cmdPath, commands[nCmd].words, environ);
            //we are still here - unable to start command - error handling
            fprintf(stderr, "Error executing external call:%s:Error:%s\n", commands[nCmd].words[0], strerror(errno));
            exit(EXIT_FAILURE);
        } else if (childpid > 0) {
            //FIXME: Maintain some information about this process so we can lookup it later
            if (thisOut >= 0) {
                log_out(3, "Close fd %d in shell\n", thisOut);
                close(thisOut);
            }
            if (thisIn >= 0) {
                log_out(3, "Close fd %d in shell\n", thisIn);
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
    log_out(2, "Wait for last command to terminate\n");
    int callstat = 0;
    pid_t r = waitpid(childpid, &callstat, 0);
    log_out(2, "Last command exited\n");
    if (r > 0) {
        if (WIFEXITED(callstat)) {
            return WEXITSTATUS(callstat);
        } else {
            return 128 | WTERMSIG(callstat);
        }
    } else { //FIXME: This is a temporary hack
        if (errno == EINTR) { //Signal does interrupt waitpid. It may be better to do this in signal handler, where waitpid
                              //is not interrupted
            errno = 0;
            r = waitpid(childpid, &callstat, 0);
            if (r > 0) {
                if (WIFEXITED(callstat)) {
                    return WEXITSTATUS(callstat);
                } else {
                    return 128 | WTERMSIG(callstat);
                }
            } else {
                perror("Error in second waitpid. This should not happen");
                return -1;
            }
        } else {
            perror("Error waiting for result");
            return -1;
        }
    }
}

static void init(int argc, char *argv[]) {
    int i;
    trsh_status.status = 0;
    trsh_status.interactive = isatty(fileno(stdin)) && isatty(fileno(stdout));
    errno = 0; //Clear eventually set ENOTTY
    trsh_status.prog = argv[0];
    trsh_status.nPosArgs = 0;
    int logLevel;
    while ((i = getopt(argc, argv, ":L:")) != -1) {
        switch (i) {
            case 'L':
                //FIXME: Use strtol
                if ((logLevel = *optarg - '0') >= 0 && logLevel <= 9) {
                    log_setLevel(logLevel);
                } else {
                    log_out(0, "Invalid log level:%s\n", optarg);
                }
                break;
            default:
                break;
        }
    }
    argc -= optind;
    argv += optind;
    trsh_status.environ = (char **) malloc(sizeof(char *));
    if (trsh_status.environ == NULL) {
        log_out(0, "Out of memory in init\n");
        _exit(1);
    }
    trsh_status.environ[0] = NULL;

    if (argc > 0) {
        trsh_status.interactive = 0;
        trsh_status.script = argv[0];
        if (freopen(trsh_status.script, "r", stdin) == NULL) {
            fprintf(stderr, "Cannot open file '%s':%s", trsh_status.script, strerror(errno));
            _exit(1);
        }
        //FIXME: Now set positional parameters from the remaining arguments
        //This will be a non interactive shell and we have to fill $1, $2, ...
        //FIXME: The freopen should in case of a script make sure that script is read by the shell
        //This did work, although some further review should be done
    }
    if (trsh_status.interactive) {
        trsh_status.sigINT.sa_flags = 0;
        sigemptyset(&trsh_status.sigINT.sa_mask);
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
        log_out(1, "Sheel loop: Parse line...:%s.\n", line);
        struct cmd_simpleCmd *multiCmd;
        int numCmd = cmd_parse(line, &multiCmd);
        if (numCmd < 0) {
            fprintf(stderr, "Error parsing command\n");
            status = 1;
        } else if (numCmd == 0) {
            log_out(2, "Ignore empty line\n");
            continue;
        } else {
            log_out(1, "Num commands:%d\n", numCmd);
            for (i=0; i < numCmd;i++) {
                if (multiCmd[i].words == NULL) {
                    log_out(1, "<empty command> - only environment");
                } else {
                    log_out(1, "Command:%s\n", multiCmd[i].words[0]);
                    for (j=1; multiCmd[i].words[j] != NULL; j++) {
                        log_out(1, "arg %d:%s.\n", j, multiCmd[i].words[j]);
                    }
                }
                log_out(1, "Continued with:%s\n", cmd_showNext(multiCmd[i].next));
            }
            //find pipes and process pipe by pipe
            int runNext = RUN_ALWAYS;
            for (i = 0; i < numCmd;) {
                //Find end of this list of commands. Pipes must be run in once, connected.
                //When command is not terminated with pipe, then we run it up to exactly this position.
                //j is the position of the last command to be run together with this command
                for (j = i; multiCmd[j].next == CMD_PIPE; j++); //FIXME: If parser fails checking command ending with pipe, overflow!
                if (runNext == RUN_ALWAYS || status == 0 && runNext == RUN_IF_OK || status != 0 && runNext == RUN_IF_ERROR) {
                    status = runPipe(multiCmd + i, j - i);
                    log_out(1, "Return code:%d\n", status);
                    if (status < 0) {
                        trsh_status.status = 255;
                        break; //severe error did happen, break this command chain
                    } else {
                        trsh_status.status = status;
                    }
                }
                if (multiCmd[j].next == CMD_TRUE) {
                    runNext = RUN_IF_OK;
                } else if (multiCmd[j].next == CMD_FALSE) {
                    runNext = RUN_IF_ERROR;
                } else {
                    runNext = RUN_ALWAYS;
                }
                i = j + 1;
            }
            cmd_free(multiCmd, numCmd);
        }
    }
    return trsh_status.status;
}
