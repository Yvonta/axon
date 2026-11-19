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
#include "log.h"
#include "buspackage.h"
#include "wssessionmanager.h"
#include "packagering.h"



typedef struct 
{
    struct lws_context *context;
    void (*funptr)(WSSESSION *session, struct lws *wsi, BUSPACKAGE *bp);
    WSSESSIONMANAGER *sm;
} WEBSOCKETSERVER;

WEBSOCKETSERVER *WEBSOCKETSERVER_CREATE(int port, void (*funptr)(WSSESSION *session, struct lws *wsi, BUSPACKAGE *bp));
void WEBSOCKETSERVER_DESTROY(WEBSOCKETSERVER *v);
void WEBSOCKETSERVER_TICK(WEBSOCKETSERVER *v);
int WEBSOCKETSERVER_SEND(struct lws *wsi, BUSPACKAGE *bp);
void WEBSOCKETSERVER_SENDALL(WEBSOCKETSERVER *v, BUSPACKAGE *bp);
int WEBSOCKETSERVER_SENDCHAT(WEBSOCKETSERVER *v, struct lws *wsi, char *msg);
void WEBSOCKETSERVER_SENDCHATALL(WEBSOCKETSERVER *v, char *msg);
int WEBSOCKETSERVER_SENDPCM(WEBSOCKETSERVER *v, struct lws *wsi, char *pcm, int len);
void WEBSOCKETSERVER_SENDPCMALL(WEBSOCKETSERVER *v, char *pcm, int len);
void WEBSOCKETSERVER_SENDPCMOTHERS(WEBSOCKETSERVER *v, struct lws *wsi, char *pcm, int len);


