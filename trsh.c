#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include "cmdline.h"
#include "trsh.h"

struct trsh_stat trsh_status;

extern char **environ;

static void sigHandler(int sig) {
    //FIXME: This should do something: Wait for all currently running processes terminating
    fprintf(stdout, "\n");
}

static int iCd(int, char *[]);
static int iExport(int, char *[]);
const char *shlFunc[] = { "cd", "export" };

int (*shlCall[])(int, char *[]) = {
    iCd,
    iExport
};

static int iCd(int argc, char *argv[]) {
    return 0;
}

static int iExport(int argc, char *argv[]) {
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
        const char **intCmd = (const char **) bsearch((void *) name, (void *) shlFunc, sizeof(shlFunc) / sizeof(shlFunc[0]),
                                sizeof(char *), (int (*)(const void *, const void *)) srchString);
        if (intCmd != NULL) {
            fprintf(stderr, "Internal command:%s, num %d\n", *name, intCmd - shlFunc);
        }
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
    Run a pipe or a simple command.
 */
static int runPipe(struct cmd_simpleCmd *commands, int maxCmd) {
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
                status = runPipe(multiCmd + i, j - i);
fprintf(stderr, "Return code:%d\n", status);
                //FIXME: Now base decision for next command on return code
                i = j + 1;
            }
            cmd_free(multiCmd, numCmd);
        }
    }
    return status;
}
