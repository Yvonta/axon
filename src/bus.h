#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>


#include "config.h"
#include "commons.h"
#include "buspackage.h"

#define QUEUE_SIZE 5
#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 2
#define TOTAL_MESSAGES 6 // Aantal berichten per producent

// Structuur voor het bussysteem (Circular Buffer / Wachtrij)
typedef struct {
    BUSPACKAGE *buffer;
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} BUS;


BUS *BUS_CREATE();
void BUS_DESTROY(BUS *v);
void BUS_PUT(BUS *bus, BUSPACKAGE *item);
BUSPACKAGE *BUS_GET(BUS *bus);



