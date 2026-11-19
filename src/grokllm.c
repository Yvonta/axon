#include "grokllm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <pthread.h>


// Context passed to worker thread per streaming session
typedef struct {
    GROKLLM *client;

    // Dynamic streaming buffers
    char *sse_buffer;
    size_t sse_buffer_len;

    char *sentence_buffer;
    size_t sentence_buffer_len;

    char *full_response_buffer;
    size_t full_response_len;
} grok_request_t;

static int append_to_buffer(char **buffer, size_t *buffer_len, const char *data, size_t len) {
    char *new_ptr = realloc(*buffer, *buffer_len + len + 1);
    if (!new_ptr) {
        E("ERROR: realloc failed in append_to_buffer");
        return 0;
    }

    *buffer = new_ptr;
    memcpy(*buffer + *buffer_len, data, len);
    *buffer_len += len;
    (*buffer)[*buffer_len] = '\0';
    return 1;
}

static void extract_sentences(grok_request_t *req, const char *text) {
    if (!text || strlen(text) == 0) return;

    if (!append_to_buffer(&req->sentence_buffer, &req->sentence_buffer_len, text, strlen(text))) {
        return;
    }

    char *start = req->sentence_buffer;
    char *p = start;

    while (*p != '\0') {
        if ((*p == '.' || *p == '!' || *p == '?') && 
            (*(p + 1) == ' ' || *(p + 1) == '\n' || *(p + 1) == '\0' || *(p + 1) == '\t')) {
            
            size_t sentence_len = (p - start) + 1;
            char *sentence = malloc(sentence_len + 1);
            if (sentence) {
                memcpy(sentence, start, sentence_len);
                sentence[sentence_len] = '\0';

                E("Extracted sentence: %s", sentence);
                if (req->client->on_sentence) {
                    req->client->on_sentence(sentence, req->client->userdata);
                }
                free(sentence);
            }

            p++;
            while (*p == ' ' || *p == '\t' || *p == '\n') {
                p++;
            }
            start = p;
        } else {
            p++;
        }
    }

    if (start != req->sentence_buffer) {
        size_t rem_len = strlen(start);
        memmove(req->sentence_buffer, start, rem_len + 1);
        req->sentence_buffer_len = rem_len;
    }
}

static void parse_sse_line(grok_request_t *req, const char *line) {
    if (strncmp(line, "data:", 5) != 0) return;
    const char *json_str = line + 5;
    while (*json_str == ' ') json_str++;

    if (strcmp(json_str, "[DONE]") == 0) {
        E("Received [DONE] stream marker.");
        if (req->sentence_buffer_len > 0 && req->client->on_sentence) {
            req->client->on_sentence(req->sentence_buffer, req->client->userdata);
            req->sentence_buffer_len = 0;
            if (req->sentence_buffer) req->sentence_buffer[0] = '\0';
        }
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        E("Failed to parse SSE JSON: %s", json_str);
        return;
    }

    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (error) {
        cJSON *err_msg = cJSON_GetObjectItemCaseSensitive(error, "message");
        if (cJSON_IsString(err_msg) && err_msg->valuestring) {
            E("API ERROR RETURNED: %s", err_msg->valuestring);
        } else {
            E("API ERROR RETURNED (Unknown format)");
        }
        cJSON_Delete(root);
        return;
    }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (cJSON_IsArray(choices)) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        if (first_choice) {
            cJSON *delta = cJSON_GetObjectItemCaseSensitive(first_choice, "delta");
            if (delta) {
                cJSON *content = cJSON_GetObjectItemCaseSensitive(delta, "content");
                if (cJSON_IsString(content) && (content->valuestring != NULL)) {
                    const char *token = content->valuestring;
                    
                    // Accumulate complete assistant message
                    append_to_buffer(&req->full_response_buffer, &req->full_response_len, token, strlen(token));

                    if (req->client->on_token) {
                        req->client->on_token(token, req->client->userdata);
                    }
                    extract_sentences(req, token);
                }
            }
        }
    }

    cJSON_Delete(root);
}

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    grok_request_t *req = (grok_request_t *)userdata;

    if (!append_to_buffer(&req->sse_buffer, &req->sse_buffer_len, ptr, total_size)) {
        return 0;
    }

    char *line_start = req->sse_buffer;
    char *line_end;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        *line_end = '\0';
        
        if (line_end > line_start && *(line_end - 1) == '\r') {
            *(line_end - 1) = '\0';
        }

        if (strlen(line_start) > 0) {
            parse_sse_line(req, line_start);
        }

        line_start = line_end + 1;
    }

    size_t processed_len = line_start - req->sse_buffer;
    size_t remaining_len = req->sse_buffer_len - processed_len;

    if (remaining_len > 0) {
        memmove(req->sse_buffer, line_start, remaining_len);
    }
    req->sse_buffer_len = remaining_len;
    req->sse_buffer[req->sse_buffer_len] = '\0';

    return total_size;
}

static void free_request(grok_request_t *req) {
    if (!req) return;
    free(req->sse_buffer);
    free(req->sentence_buffer);
    free(req->full_response_buffer);
    free(req);
}

static void *grok_worker_thread(void *arg) {
    grok_request_t *req = (grok_request_t *)arg;
    GROKLLM *client = req->client;

    E("Worker thread starting stream session with history length: %zu messages", client->message_count);

    CURL *curl = curl_easy_init();
    if (!curl) {
        E("ERROR: curl_easy_init failed");
        free_request(req);
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        E("ERROR: cJSON_CreateObject failed");
        curl_easy_cleanup(curl);
        free_request(req);
        return NULL;
    }

    cJSON_AddStringToObject(root, "model", client->model);
    cJSON_AddBoolToObject(root, "stream", 1);
    cJSON_AddNumberToObject(root, "temperature", client->temperature);

    // Build history message array dynamically
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    grok_message_t *curr = client->history_head;
    while (curr) {
        cJSON *msg_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_obj, "role", curr->role);
        cJSON_AddStringToObject(msg_obj, "content", curr->content);
        cJSON_AddItemToArray(messages, msg_obj);
        curr = curr->next;
    }

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_body) {
        E("ERROR: cJSON_PrintUnformatted failed");
        curl_easy_cleanup(curl);
        free_request(req);
        return NULL;
    }

    struct curl_slist *headers = NULL;
    char auth_hdr[256];
    int auth_len = snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", client->api_key);
    if (auth_len < 0 || (size_t)auth_len >= sizeof(auth_hdr)) {
        E("ERROR: Auth header format failed");
        free(json_body);
        curl_easy_cleanup(curl);
        free_request(req);
        return NULL;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_hdr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.x.ai/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, req);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        E("CURL ERROR: %s (code %d)", curl_easy_strerror(res), res);
    } else {
        E("Curl stream finished with HTTP status code: %ld", http_code);
    }

    // Save assistant response to memory so next request remembers it
    if (req->full_response_buffer && req->full_response_len > 0) {
        GROKLLM_ADDMESSAGE(client, "assistant", req->full_response_buffer);
        if (client->on_complete) {
            client->on_complete(req->full_response_buffer, client->userdata);
        }
    }

    free(json_body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free_request(req);

    E("Worker thread exiting.");
    return NULL;
}

GROKLLM *GROKLLM_CREATE(const char *api_key, const char *model) 
{
    E("Initializing grok client with model: %s", model ? model : "grok-4.7");
    GROKLLM *client = calloc(1, sizeof(GROKLLM));
    if(!client) 
    {
      return NULL;
    }
    client->api_key = strdup(api_key ? api_key : "");
    client->model = strdup(model ? model : "grok-4.7");
    client->temperature = 0.7f;
    return client;
}

int GROKLLM_ADDMESSAGE(GROKLLM *v, const char *role, const char *content) 
{
    if(!v || !role || !content) 
    {
      return 0;
    }
    
    grok_message_t *msg = calloc(1, sizeof(grok_message_t));
    if(!msg)
    {
      return 0;
    }
    
    msg->role = strdup(role);
    msg->content = strdup(content);

    if(!v->history_head) 
    {
        v->history_head = msg;
        v->history_tail = msg;
    } 
    else 
    {
        v->history_tail->next = msg;
        v->history_tail = msg;
    }

    v->message_count++;
    return 1;
}

void GROKLLM_CLEARHISTORY(GROKLLM *v) {
    if(!v)
    {
      return;
    }
    
    grok_message_t *curr = v->history_head;
    while(curr) 
    {
        grok_message_t *next = curr->next;
        free(curr->role);
        free(curr->content);
        free(curr);
        curr = next;
    }

    v->history_head = NULL;
    v->history_tail = NULL;
    v->message_count = 0;
    E("Cleared client conversation history.");
}

void GROKLLM_DESTROY(GROKLLM *v) 
{
    if(!v)
    {
      return;
    }
    
    E("Freeing grok client instance");
    GROKLLM_CLEARHISTORY(v);
    free(v->api_key);
    free(v->model);
    free(v);
}

int GROKLLM_STREAMCHAT(GROKLLM *v, const char *userprompt) 
{
    if(!v || !userprompt) 
    {
        E("ERROR: grok_stream_chat called with invalid parameters");
        return -1;
    }

    // Automatically append user prompt to conversation context
    if(!GROKLLM_ADDMESSAGE(v, "user", userprompt)) 
    {
        return -1;
    }

    grok_request_t *req = calloc(1, sizeof(grok_request_t));
    if(!req) 
    {
        E("ERROR: Failed to allocate grok_request_t");
        return -1;
    }

    req->client = v;

    pthread_t thread;
    if(pthread_create(&thread, NULL, grok_worker_thread, req) == 0) 
    {
        pthread_detach(thread);
        E("Successfully spawned stream worker thread.");
        return 0;
    } 
    else 
    {
        E("ERROR: pthread_create failed");
        free_request(req);
        return -1;
    }
}
