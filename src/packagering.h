#pragma once

#include <stddef.h>
#include <string.h>
#include <stdlib.h>


#include "buspackage.h"

typedef struct {
    BUSPACKAGE *buffer;
    size_t sizesamples; // Totaal aantal shorts in de buffer
    size_t writeptr;
    size_t readptr;
    int channels;
} PACKAGERING;

PACKAGERING *PACKAGERING_CREATE(BUSPACKAGE *storage, size_t sizesamples);
void PACKAGERING_DESTROY(PACKAGERING *rb);

// Hoeveel samples (shorts) zijn er beschikbaar om te lezen?
size_t PACKAGERING_AVAILABLEREAD(PACKAGERING *rb);

// Hoeveel ruimte (shorts) is er nog vrij om te schrijven?
size_t PACKAGERING_AVAILABLEWRITE(PACKAGERING *rb);

// Schrijf samples naar de buffer. Retourneert aantal geschreven samples.
size_t PACKAGERING_WRITE(PACKAGERING *rb, BUSPACKAGE *data);

// Lees samples uit de buffer. Retourneert aantal gelezen samples.
size_t PACKAGERING_READ(PACKAGERING *rb, BUSPACKAGE *data);


