#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <libwebsockets.h>
#include <ctype.h>

#include "config.h"
#include "commons.h"
#include "linkedlist.h"
#include "log.h"
#include "websocketserver.h"
#include "buspackage.h"
#include "grokstt.h"
#include "grokllm.h"
#include "elevenlabstts.h"

typedef struct
{
  char *env_elkey;
  char *env_elvoice;
  char *env_elmodel;
  char *env_xaikey;
  char *env_xaisttmodel;
  char *env_xaillmmodel;
  int serial;
  BUSPACKAGE *bp;
  int pos;
  GROKSTT *gstt;
  GROKLLM *gllm;
  ELEVENLABSTTSCONFIG ttsc;
  ELEVENLABSTTS *tts;
  BOOL loop;
  int world;
  int room;
  double goal;
} APP;

APP *APP_CREATE();
void APP_DESTROY(APP *v);
BOOL APP_TICK(APP *v, WEBSOCKETSERVER *ws);
void APP_DATA(APP *v, WEBSOCKETSERVER *ws, WSSESSION *session, struct lws *wsi, BUSPACKAGE *bp);

