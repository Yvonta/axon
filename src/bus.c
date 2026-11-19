#include "bus.h"

// Initialiseer de bus
BUS *BUS_CREATE() 
{
    BUS *v = (BUS*)malloc(sizeof(BUS));
    if(!v)
    {
    	return NULL;
    }
    v->buffer = (BUSPACKAGE*)malloc(sizeof(BUSPACKAGE) * QUEUE_SIZE);
    if(!v->buffer)
    {
    	free(v);
    	return NULL;
    
    }
    
    v->head = 0;
    v->tail = 0;
    v->count = 0;
    pthread_mutex_init(&v->mutex, NULL);
    pthread_cond_init(&v->not_full, NULL);
    pthread_cond_init(&v->not_empty, NULL);

    return v;
}

// Vernietig de bus en ruim geheugen/locks op
void BUS_DESTROY(BUS *v) 
{
    if(v)
    {
    	return;
    }

    pthread_mutex_destroy(&v->mutex);
    pthread_cond_destroy(&v->not_full);
    pthread_cond_destroy(&v->not_empty);
    if(v->buffer)
    {
    	free(v->buffer);
    }
    free(v);
}

// Gegevens toevoegen aan de bus (door producenten)
void PUS_PUT(BUS *v, BUSPACKAGE *item) {
    pthread_mutex_lock(&v->mutex);

    // Wacht zolang de bus vol is
    while (v->count == QUEUE_SIZE) {
        pthread_cond_wait(&v->not_full, &v->mutex);
    }

    // Voeg het item toe aan de buffer
    memcpy(&v->buffer[v->tail], item, sizeof(BUSPACKAGE));
    v->tail = (v->tail + 1) % QUEUE_SIZE;
    v->count++;

    // Signaleer consumenten dat er data is
    pthread_cond_signal(&v->not_empty);
    pthread_mutex_unlock(&v->mutex);
}

// Gegevens uithalen uit de bus (door consumenten)
BUSPACKAGE *BUS_GET(BUS *v) {

    // Wacht zolang de bus leeg is
    while (v->count == 0) {
        pthread_cond_wait(&v->not_empty, &v->mutex);
    }

    // Haal het item op uit de buffer
    BUSPACKAGE *item = (BUSPACKAGE *)malloc(sizeof(BUSPACKAGE));
    memcpy(item, &v->buffer[v->head], sizeof(BUSPACKAGE));
    v->head = (v->head + 1) % QUEUE_SIZE;
    v->count--;

    // Signaleer producenten dat er weer plek is
    pthread_cond_signal(&v->not_full);
    pthread_mutex_unlock(&v->mutex);

    return item;
}
