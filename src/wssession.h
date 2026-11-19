#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <libwebsockets.h>

#include "buspackage.h"
#include "packagering.h"

#define RB_FRAMES 1024

typedef struct
{
  struct lws *wsi;
  BUSPACKAGE bp;
  PACKAGERING *rb;
  BUSPACKAGE *storage;
  size_t receivedbytes;
  float px;
  float py;
  float pz;
  float vx;
  float vy;
  float vz;
  float verlocity;
  int animation;
  int pose;
} WSSESSION;


WSSESSION *WSSESSION_CREATE(struct lws *wsi);
void WSSESSION_DESTROY(WSSESSION *v); 
