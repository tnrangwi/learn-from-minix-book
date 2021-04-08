#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "log.h"

static char **envStore = NULL;
static int nEnv = 0;

/**
    Search environment for give variable. Before search, do syntax check for variable.
    @param search: A search string, either null terminated key or a null terminated key=value expression
    @param keyAddr: store location where to put the string address into *keyAddr. put_env is needs this.
    @param val: address to store the null terminated value location.
    @return: Length of the key, -1 if not found and any smaller value on error.
 */
static int envGet(const char *search, char ***keyAddr, const char **val) {
    const char *p = search;
    //int setExpr = 0; /* maybe we need that later - whether incoming expression is a set or just a variable name */
    int length, n;
    char **var;
    if (search == NULL || keyAddr == NULL || val == NULL) {
        log_out(0, "Internal error - env-get s/k/v:%p/%p/%p\n", search, keyAddr, val);
        return -2;
    }
    if (p == NULL || !(*p>='A' && *p<='Z' || *p>='a' && *p<='Z' || *p=='_')) {
        log_out(0, "Invalid search string, internal error\n");
        return -2;
    }
    for (length=1,p++; *p>='A' && *p<='Z' || *p>='a' && *p<='z' || *p>='0' && *p<='9' || *p=='_'; p++,length++);
    switch (*p) {
        case '=':
            //setExpr = 1;
        case '\0':
            break;
        default:
            log_out(0, "Internal error, string is neither null terminated nor part of set:'%s'\n", search);
            return -2;
    }
    if (envStore == NULL) return -1;
    //I would like to use bsearch_s here, I could just use strncmp for the compare function.
    //But bsearch_s does not exist here and bsearch does not allow me to pass a parameter into the
    //compare function (the length). Only a global variable would help.
    for (var = envStore; *var != NULL; var++) {
        const char *s;
        for (s = *var, p = search, n=0; *p == *s && *s != '=' && n < length; s++, p++, n++);
        if (n >= length && *s == '=') {
            *keyAddr = var;
            *val = ++s;
            return n;
        }
    }
    return -1;
}

const char *env_get(const char *search) {
    char **key;
    const char *val;
    if (envGet(search, &key, &val) >= 0) {
        return val;
    }
    return NULL;
}

/**
    Place new environment setting, overwriting old if it exists.
 */
int env_put(const char *keyVal) {
    int rc;
    char **key;
    const char *val;
    char *word;
    if (keyVal == NULL) {
        log_out(0, "Internal error, NULL key for new environment setting\n");
    }
    word = (char *) malloc((strlen(keyVal) + 1) * sizeof(keyVal));
    if (word == NULL) {
        log_out(0, "No memory in environment\n");
        return -1;
    }
    if ((rc = envGet(keyVal, &key, &val)) < -1) {
        free(word);
        return -1;
    } else if(rc == -1) {
        if (envStore == NULL) {
            envStore = (char **) malloc(2 * sizeof(char *));
            if (envStore == NULL) {
                free(word);
                log_out(0, "No more memory in environment\n");
                return -1;
            }
            key = envStore;
            nEnv = 1;
        } else {
            char **tEnv = realloc(envStore, ++nEnv);
            if (tEnv == NULL) {
                log_out(0, "Out of memory when adding to environment\n");
                free(word);
                nEnv--;
                return -1;
            }
            envStore = tEnv;
            key = envStore + nEnv - 1;
       }
       envStore[nEnv] = NULL;
    } else {
        free(*key);
    }
    *key = word;
    strcpy(word, keyVal);
    return 0;
}
