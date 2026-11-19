#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <libwebsockets.h>

#include "wssession.h"

#define WSSESSIONMANAGER_MAXSESSIONS 50




typedef struct
{
   WSSESSION *sessions[WSSESSIONMANAGER_MAXSESSIONS];

} WSSESSIONMANAGER;

WSSESSIONMANAGER *WSSESSIONMANAGER_CREATE();
void WSSESSIONMANAGER_DESTROY(WSSESSIONMANAGER *v);
WSSESSION *WSSESSIONMANAGER_NEW(WSSESSIONMANAGER *v, struct lws *wsi);
WSSESSION *WSSESSIONMANAGER_GET(WSSESSIONMANAGER *v, struct lws *wsi);
void WSSESSIONMANAGER_END(WSSESSIONMANAGER *v, struct lws *wsi);
