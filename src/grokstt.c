#include "grokstt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GROKSTT* GROKSTT_CREATE(const char *api_key, const char *model, const char *encoding) {
    GROKSTT *ctx = (GROKSTT*)malloc(sizeof(GROKSTT));
    if (!ctx) return NULL;

    ctx->api_key = strdup(api_key);
    ctx->model = model ? strdup(model) : strdup("grok-voice-transcribe-2.0");
    ctx->encoding = encoding ? strdup(encoding) : strdup("pcm");
    ctx->curl = NULL;
    ctx->connected = false;

    curl_global_init(CURL_GLOBAL_ALL);
    return ctx;
}

bool GROKSTT_CONNECT(GROKSTT *ctx) {
    if (!ctx) return false;

    ctx->curl = curl_easy_init();
    if (!ctx->curl) return false;

    // Build URL with query parameters for configuration
    char url[512];
    snprintf(url, sizeof(url), "wss://api.x.ai/v1/stt?model=%s&encoding=%s", ctx->model, ctx->encoding);

    // Setup headers including Authorization Bearer token
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", ctx->api_key);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, headers);
    
    // Tell libcurl we want to use low-level socket send/recv operations (CONNECT_ONLY)
    curl_easy_setopt(ctx->curl, CURLOPT_CONNECT_ONLY, 2L); // 2L enables WebSocket negotiation for wss://

    CURLcode res = curl_easy_perform(ctx->curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        fprintf(stderr, "Grok STT Connection failed: %s\n", curl_easy_strerror(res));
        return false;
    }

    ctx->connected = true;
    return true;
}

bool GROKSTT_SENDAUDIO(GROKSTT *ctx, const unsigned char *data, size_t len) {
    if (!ctx || !ctx->connected) return false;

    size_t sent = 0;
    CURLcode res = curl_ws_send(ctx->curl, data, len, &sent, 0, CURLWS_BINARY);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to send audio chunk: %s\n", curl_easy_strerror(res));
        return false;
    }
    return true;
}

bool GROKSTT_FINISH(GROKSTT *ctx) {
    if (!ctx || !ctx->connected) return false;
    
    size_t sent = 0;
    // Send close or empty binary frame with control flags if required, 
    // or standard WebSocket close payload.
    CURLcode res = curl_ws_send(ctx->curl, "", 0, &sent, 0, CURLWS_CLOSE);
    return (res == CURLE_OK);
}

/*int GROKSTT_RECIEVE(GROKSTT *ctx, char *buffer, size_t max_len, size_t *out_len) {
    if (!ctx || !ctx->connected) return -1;

    const struct curl_ws_frame *meta;
    CURLcode res = curl_ws_recv(ctx->curl, buffer, max_len, out_len, &meta);

    if (res == CURLE_AGAIN) {
        return 0; // No data available right now
    } else if (res != CURLE_OK) {
        return -1; // Error or connection closed
    }

    return 1; // Success, check *out_len and buffer for incoming JSON text events
}*/

int GROKSTT_RECIEVE(GROKSTT *ctx, char *buffer, size_t max_len, size_t *out_len) {
    if (!ctx || !ctx->connected) return -1;

    const struct curl_ws_frame *meta;
    CURLcode res = curl_ws_recv(ctx->curl, buffer, max_len, out_len, &meta);

    if (res == CURLE_AGAIN) {
        return 0; // No data available right now (non-blocking)
    } else if (res != CURLE_OK) {
        return -1; // Error or connection closed
    }

    // Prevent returning success when no payload data was received
    if (*out_len == 0) {
        return 0; // Treat empty frames / control frames as no active data event
    }

    // Optional: Check meta->bytesleft if dealing with large, fragmented WebSocket frames
    if (meta && meta->bytesleft > 0) {
        // Handle fragmentation if your STT payload exceeds max_len
    }

    return 1; // Success, valid payload present in buffer with length *out_len
}

void GROKSTT_DESTROY(GROKSTT *ctx) {
    if (ctx) {
        if (ctx->curl) {
            curl_easy_cleanup(ctx->curl);
        }
        free(ctx->api_key);
        free(ctx->model);
        free(ctx->encoding);
        free(ctx);
        curl_global_cleanup();
    }
}


char *GROKSTT_PARSE(char *json)
{
    if (!json) return NULL;

    char *result = NULL;
    cJSON *root = cJSON_Parse(json);
    if (root != NULL) 
    {
        // 1. Check the flags to prevent double-processing
        cJSON *speech_final = cJSON_GetObjectItemCaseSensitive(root, "speech_final");
        
        if (cJSON_IsTrue(speech_final)) 
        {
            // 2. Extract the transcription text string safely
            cJSON *text_item = cJSON_GetObjectItemCaseSensitive(root, "text");
            if (cJSON_IsString(text_item) && (text_item->valuestring != NULL)) 
            {
                char *transcript_str = text_item->valuestring;
                result = strdup(transcript_str);
            }         
        }
        cJSON_Delete(root);
    }
    
    return result; // Returns the new string, or NULL if not final/valid
}                        

bool GROKSTT_FINALIZE_TURN(GROKSTT *ctx) 
{
    if (!ctx || !ctx->connected) return false;

    const char *pkt = "{\"type\":\"finalize\"}";
    size_t sent = 0;
    
    // Send as a text frame to force the current utterance to finalize immediately
    CURLcode res = curl_ws_send(ctx->curl, pkt, strlen(pkt), &sent, 0, CURLWS_TEXT);
    return (res == CURLE_OK);
}                        
