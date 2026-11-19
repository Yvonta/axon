#pragma once

#include <stddef.h>

#include "config.h"
#include "commons.h"
#include "linkedlist.h"
#include "log.h"

// Forward declaration
typedef struct grokllm GROKLLM;

// Message structure for maintaining chat history
typedef struct grok_message {
    char *role;                // "system", "user", or "assistant"
    char *content;             // Message body content
    struct grok_message *next; // Pointer to the next message in the chain
} grok_message_t;

// Callback type signatures
typedef void (*grok_token_cb)(const char *token, void *userdata);
typedef void (*grok_sentence_cb)(const char *sentence, void *userdata);
typedef void (*grok_complete_cb)(const char *full_text, void *userdata);

// Core client state structure
struct grokllm {
    char *api_key;
    char *model;
    float temperature;

    // Persistent Chat History Linked List
    grok_message_t *history_head;
    grok_message_t *history_tail;
    size_t message_count;

    // Callbacks
    grok_token_cb on_token;
    grok_sentence_cb on_sentence;
    grok_complete_cb on_complete;
    void *userdata;
};

// Initializes a new Grok LLM client instance.
GROKLLM *GROKLLM_CREATE(const char *api_key, const char *model);

// Frees the client instance along with all stored conversation history.
void GROKLLM_DESTROY(GROKLLM *v);

// Manually adds a message (system, user, or assistant) to the chat history.
int GROKLLM_ADDMESSAGE(GROKLLM *v, const char *role, const char *content);

// Clears all existing conversation history while keeping client configurations.
void GROKLLM_CLEARHISTORY(GROKLLM *v);

// Appends a user prompt to chat history and asynchronously streams the complete 
// context via background worker thread. Automatically appends assistant response upon completion.
int GROKLLM_STREAMCHAT(GROKLLM *v, const char *userprompt);

