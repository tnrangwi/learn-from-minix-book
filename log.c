#include <stdio.h>
#include <stdarg.h>
#include "log.h"

static int log_level = 0;

void log_out(int level, const char *msg, ...) {
    va_list args;
    if (log_level >= level) {
        va_start(args, msg);
        vfprintf(stderr, msg, args);
        va_end(args);
    }
}

void log_setLevel(int level) { log_level = level; }

int log_getLevel() { return log_level; }
