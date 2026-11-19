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
#include "log.h"

typedef struct __attribute__((packed)) buspackage {
    uint32_t fixed;         // Offset 0
    uint16_t version;       // Offset 4
    uint16_t type;          // Offset 6
    uint32_t serial;        // Offset 8
    uint32_t payloadlen;    // Offset 12
    uint8_t  payload[4096]; // Offset 16
} BUSPACKAGE;

BOOL BUSPACKAGE_TEST(BUSPACKAGE *bp);
BUSPACKAGE *BUSPACKAGE_CREATE();
void BUSPACKAGE_DESTROY(BUSPACKAGE *v);

