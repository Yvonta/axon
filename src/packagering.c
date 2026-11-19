#include "packagering.h"

PACKAGERING *PACKAGERING_CREATE(BUSPACKAGE *storage, size_t sizesamples) {
    PACKAGERING *v = malloc(sizeof(PACKAGERING));
    if(!v)
    {
      return NULL;
    }
    memset(v, 0, sizeof(PACKAGERING));
    v->buffer = storage;
    v->sizesamples = sizesamples;
    v->channels = 1;
    v->writeptr = 0;
    v->readptr = 0;
    if(v->buffer) 
    {
       memset(v->buffer, 0, sizesamples * sizeof(BUSPACKAGE));
    }
    return v;
}

void PACKAGERING_DESTROY(PACKAGERING *v) 
{
    if(!v)
    {
      return;
    }
    v->writeptr = 0;
    v->readptr = 0;
    free(v);
}

size_t PACKAGERING_AVAILABLEREAD(PACKAGERING *v) 
{
    if(v->writeptr >= v->readptr) 
    {
        return v->writeptr - v->readptr;
    }
    return v->sizesamples - (v->readptr - v->writeptr);
}

size_t PACKAGERING_AVAILABLEWRITE(PACKAGERING *v) 
{
    // We laten altijd 1 sample leeg om read/write pointer onderscheid te maken
    return v->sizesamples - PACKAGERING_AVAILABLEREAD(v) - 1;
}

size_t PACKAGERING_WRITE(PACKAGERING *v, BUSPACKAGE *data) 
{
    int numsamples = 1;
    size_t available = PACKAGERING_AVAILABLEWRITE(v);
    if(numsamples > available)
    {
      numsamples = available;
    }
    for(size_t i = 0; i < numsamples; i++) 
    {
        v->buffer[v->writeptr] = data[i];
        v->writeptr = (v->writeptr + 1) % v->sizesamples;
    }
    return numsamples;
}

size_t PACKAGERING_READ(PACKAGERING *v, BUSPACKAGE *data) 
{
    int numsamples = 1;
    size_t available = PACKAGERING_AVAILABLEREAD(v);
    if(numsamples > available) 
    {
      numsamples = available;
    }
    for(size_t i = 0; i < numsamples; i++) 
    {
        data[i] = v->buffer[v->readptr];
        v->readptr = (v->readptr + 1) % v->sizesamples;
    }
    return numsamples;
}


