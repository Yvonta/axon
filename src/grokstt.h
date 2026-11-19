#pragma once

#include <curl/curl.h>
#include <stdbool.h>
#include <cjson/cJSON.h>

#include "config.h"
#include "commons.h"
#include "linkedlist.h"
#include "log.h"

typedef struct {
    CURL *curl;
    char *api_key;
    char *model;
    char *encoding;
    bool connected;
} GROKSTT;

/**
 * Initialize the Grok STT context.
 */
GROKSTT* GROKSTT_CREATE(const char *api_key, const char *model, const char *encoding);

/**
 * Open the real-time WebSocket connection to Grok STT.
 */
bool GROKSTT_CONNECT(GROKSTT *ctx);

/**
 * Send a chunk of raw audio binary data to the server.
 */
bool GROKSTT_SENDAUDIO(GROKSTT *ctx, const unsigned char *data, size_t len);

/**
 * Signal that all audio has been sent (audio.done).
 */
bool GROKSTT_FINISH(GROKSTT *ctx);

/**
 * Poll/Receive incoming JSON transcript events from Grok.
 * Non-blocking or short timeout read depending on curl socket options.
 */
int GROKSTT_RECIEVE(GROKSTT *ctx, char *buffer, size_t max_len, size_t *out_len);

/**
 * Clean up and close the connection.
 */
void GROKSTT_DESTROY(GROKSTT *ctx);


char *GROKSTT_PARSE(char *json);

bool GROKSTT_FINALIZE_TURN(GROKSTT *ctx);
