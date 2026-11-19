#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "config.h"
#include "commons.h"

typedef struct LINKEDLIST_NODE {
    void *data;
    void *next;
} LINKEDLIST_NODE;

typedef struct LINKEDLIST {
    LINKEDLIST_NODE *list;
    LINKEDLIST_NODE *node;
} LINKEDLIST;

LINKEDLIST * LINKEDLIST_CREATE();
void LINKEDLIST_DESTROY(LINKEDLIST *v);
void LINKEDLIST_INSERT(LINKEDLIST *v, void* data);
LINKEDLIST *LINKEDLIST_REVERT(LINKEDLIST *v);
BOOL LINKEDLIST_WALK(LINKEDLIST *v);
void *LINKEDLIST_GET(LINKEDLIST *v);
void LINKEDLIST_RESET(LINKEDLIST *v);
void LINKEDLIST_FREEOBJECT(LINKEDLIST *v, void *object, void (*freeData)(void * object, void *data));
void LINKEDLIST_FREE(LINKEDLIST *v, void (*freeData)( void *data ));
void LINKEDLIST_FREEFUN(void *data);
void* LINKEDLIST_POPOLDEST(LINKEDLIST *v);
