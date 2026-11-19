#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <libwebsockets.h>

#include "config.h"
#include "commons.h"
#include "linkedlist.h"

#define LOG_MAX_PATH 1024

#define E(...) LOG_MSG(__VA_ARGS__)

CRESULT LOG_CREATE(BOOL write, BOOL mem, BOOL std, int (*logfuncptr)(char *));
void LOG_DESTROY();
CRESULT LOG_MSG(const char *fmt, ...);
CRESULT LOG_POLL(char *outbuffer, int maxsize);

