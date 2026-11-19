/*
 * elevenlabs_tts.c - see elevenlabs_tts.h
 *
 * Design:
 *   producer thread(s) --submit--> [input queue] --> worker thread --curl-->
 *   ElevenLabs /v1/text-to-speech/{voice}/stream?output_format=pcm_16000
 *   worker write callback --> 4096-byte blocks --> [output ring] --read--> consumer
 *
 * One libcurl easy handle is reused for the whole lifetime, so the TLS
 * connection stays open between sentences. The write callback pushes a block
 * as soon as 4096 bytes are available; nothing waits for the full response.
 */
#define _POSIX_C_SOURCE 200809L

#include "elevenlabstts.h"

#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------------ */
/* Reference counted sentence                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    el_tts_sentence_t pub;      /* must be first */
    atomic_int        refs;
    char              buf[];
} sentence_impl_t;

static sentence_impl_t *sentence_new(const char *text)
{
    size_t n = strlen(text);
    sentence_impl_t *s = malloc(sizeof *s + n + 1);
    if (!s) {
        E("sentence_new: allocation failed for length %zu", n);
        return NULL;
    }
    memcpy(s->buf, text, n + 1);
    s->pub.id       = 0;
    s->pub.text     = s->buf;
    s->pub.text_len = n;
    atomic_init(&s->refs, 1);
    return s;
}

static const el_tts_sentence_t *sentence_ref(const el_tts_sentence_t *p)
{
    if (p) {
        int old = atomic_fetch_add(&((sentence_impl_t *)p)->refs, 1);
        // E("sentence_ref: ptr=%p refs %d -> %d", (void*)p, old, old + 1);
    }
    return p;
}

static void sentence_unref(const el_tts_sentence_t *p)
{
    if (!p) return;
    sentence_impl_t *s = (sentence_impl_t *)p;
    int old = atomic_fetch_sub(&s->refs, 1);
    // E("sentence_unref: ptr=%p refs %d -> %d", (void*)p, old, old - 1);
    if (old == 1) {
        // E("sentence_unref: freeing sentence ptr=%p", (void*)p);
        free(s);
    }
}

/* ------------------------------------------------------------------------ */
/* Client state                                                             */
/* ------------------------------------------------------------------------ */

typedef struct in_node {
    const el_tts_sentence_t *s;
    struct in_node          *next;
} in_node_t;

struct elevenlabstts {
    /* config (owned copies) */
    char  *api_key, *voice_id, *model_id, *base_url, *url;
    double stability, similarity;
    bool   use_context, prewarm;
    long   connect_timeout_ms;

    pthread_mutex_t mu;
    pthread_cond_t  in_cv;           /* input queue got an item / closed / shutdown */
    pthread_cond_t  out_not_empty;   /* output ring got a block / worker done       */
    pthread_cond_t  out_not_full;    /* output ring has room / cancel / shutdown    */

    pthread_t worker;
    bool      worker_started;
    bool      worker_done;

    in_node_t *in_head, *in_tail;
    bool        input_closed;
    uint64_t   next_id;

    atomic_bool       shutdown;
    atomic_uint_fast64_t generation;   /* bumped by el_tts_cancel() */

    ELEVENLABSTTSBLOCK *ring;
    size_t           cap, head, count;

    CURL                *curl;
    struct curl_slist  *hdrs;
};

typedef struct {
    ELEVENLABSTTS            *ctx;
    uint_fast64_t             gen;
    const el_tts_sentence_t *sent;
    uint8_t                   fill[EL_TTS_BLOCK_SIZE];
    size_t                    fill_len;
    uint32_t                  index;
    long                      http_status;
    bool                      status_checked;
    char                      errbody[96];
    size_t                    errlen;
} job_t;

static bool job_aborted(const job_t *j)
{
    bool sd = atomic_load(&j->ctx->shutdown);
    bool gen_changed = atomic_load(&j->ctx->generation) != j->gen;
    if (sd || gen_changed) {
        // E("job_aborted: true (shutdown=%d, gen_changed=%d)", sd, gen_changed);
    }
    return sd || gen_changed;
}

/* ------------------------------------------------------------------------ */
/* Small string builder + JSON helpers                                      */
/* ------------------------------------------------------------------------ */

typedef struct { char *p; size_t len, cap; bool oom; } sb_t;

static void sb_put(sb_t *s, const char *d, size_t n)
{
    if (s->oom) return;
    if (s->len + n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap : 256;
        while (nc < s->len + n + 1) nc *= 2;
        char *np = realloc(s->p, nc);
        if (!np) { 
            E("sb_put: OOM realloc failed for capacity %zu", nc);
            s->oom = true; 
            return; 
        }
        s->p = np; s->cap = nc;
    }
    memcpy(s->p + s->len, d, n);
    s->len += n;
    s->p[s->len] = '\0';
}

static void sb_str(sb_t *s, const char *c) { sb_put(s, c, strlen(c)); }

static void sb_json_str(sb_t *s, const char *t)
{
    sb_put(s, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        switch (*p) {
        case '"':  sb_str(s, "\\\""); break;
        case '\\': sb_str(s, "\\\\"); break;
        case '\n': sb_str(s, "\\n");  break;
        case '\r': sb_str(s, "\\r");  break;
        case '\t': sb_str(s, "\\t");  break;
        default:
            if (*p < 0x20) {
                char e[8];
                snprintf(e, sizeof e, "\\u%04x", *p);
                sb_str(s, e);
            } else {
                sb_put(s, (const char *)p, 1);   /* UTF-8 passes through */
            }
        }
    }
    sb_put(s, "\"", 1);
}

/* locale independent "0.123" formatting */
static void sb_fixed3(sb_t *s, double v)
{
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    int m = (int)(v * 1000.0 + 0.5);
    char b[16];
    snprintf(b, sizeof b, "%d.%03d", m / 1000, m % 1000);
    sb_str(s, b);
}

static char *dupstr(const char *s) { return s ? strdup(s) : NULL; }

/* ------------------------------------------------------------------------ */
/* Output ring                                                              */
/* ------------------------------------------------------------------------ */

/* Returns false if the job was aborted (cancel/shutdown) while waiting. */
static bool push_block(job_t *j, const uint8_t *data, size_t size, bool last,
                        el_tts_status_t st, const char *msg)
{
    ELEVENLABSTTS *c = j->ctx;

    pthread_mutex_lock(&c->mu);
    while (c->count == c->cap && !job_aborted(j)) {
        // E("push_block: ring full (count=%zu, cap=%zu), waiting...", c->count, c->cap);
        pthread_cond_wait(&c->out_not_full, &c->mu);
    }
    if (job_aborted(j)) {
        E("push_block: aborted while waiting for ring space");
        pthread_mutex_unlock(&c->mu);
        return false;
    }

    ELEVENLABSTTSBLOCK *b = &c->ring[(c->head + c->count) % c->cap];
    if (size) memcpy(b->data, data, size);
    b->size        = size;
    b->sentence    = sentence_ref(j->sent);
    b->index       = j->index;
    b->first       = (j->index == 0);
    b->last        = last;
    b->status      = st;
    b->http_status = j->http_status;
    b->error_msg[0] = '\0';
    if (msg) snprintf(b->error_msg, sizeof b->error_msg, "%s", msg);
    
    E("push_block: pushed block index=%u, size=%zu, last=%d, status=%d, http=%ld", 
           b->index, b->size, b->last, b->status, b->http_status);

    j->index++;
    c->count++;

    pthread_cond_signal(&c->out_not_empty);
    pthread_mutex_unlock(&c->mu);
    return true;
}

/* ------------------------------------------------------------------------ */
/* libcurl callbacks                                                        */
/* ------------------------------------------------------------------------ */

static size_t write_cb(char *ptr, size_t sz, size_t nm, void *ud)
{
    job_t *j = ud;
    size_t n = sz * nm;

    if (job_aborted(j)) {
        E("write_cb: job aborted, returning 0 to cancel transfer");
        return 0;            /* -> CURLE_WRITE_ERROR */
    }

    if (!j->status_checked) {
        curl_easy_getinfo(j->ctx->curl, CURLINFO_RESPONSE_CODE, &j->http_status);
        j->status_checked = true;
        E("write_cb: first chunk received, HTTP status code: %ld", j->http_status);
    }

    if (j->http_status != 200) {                /* error body (JSON), not audio */
        size_t room = sizeof j->errbody - 1 - j->errlen;
        size_t k = n < room ? n : room;
        memcpy(j->errbody + j->errlen, ptr, k);
        j->errlen += k;
        j->errbody[j->errlen] = '\0';
        E("write_cb: captured error body chunk (%zu bytes): %s", k, ptr);
        return n;
    }

    size_t off = 0;
    while (off < n) {
        size_t take = EL_TTS_BLOCK_SIZE - j->fill_len;
        if (take > n - off) take = n - off;
        memcpy(j->fill + j->fill_len, ptr + off, take);
        j->fill_len += take;
        off += take;
        if (j->fill_len == EL_TTS_BLOCK_SIZE) {
            if (!push_block(j, j->fill, EL_TTS_BLOCK_SIZE, false, EL_TTS_OK, NULL))
                return 0;
            j->fill_len = 0;
        }
    }
    return n;
}

static int xfer_cb(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    bool aborted = job_aborted((job_t *)ud);
    if (aborted) {
        E("xfer_cb: abortion detected via generation/shutdown flag");
    }
    return aborted ? 1 : 0;    /* nonzero aborts the transfer */
}

/* ------------------------------------------------------------------------ */
/* Worker                                                                   */
/* ------------------------------------------------------------------------ */

static void prewarm(ELEVENLABSTTS *c)
{
    E("prewarm: starting request to base_url: %s", c->base_url);
    curl_easy_setopt(c->curl, CURLOPT_URL, c->base_url);
    curl_easy_setopt(c->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(c->curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT_MS, 4000L);
    CURLcode rc = curl_easy_perform(c->curl);
    E("prewarm: finished with curl code: %d (%s)", rc, curl_easy_strerror(rc));
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT_MS, 0L);
    curl_easy_setopt(c->curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(c->curl, CURLOPT_NOPROGRESS, 0L);
}

static void run_job(ELEVENLABSTTS *c, const el_tts_sentence_t *cur,
                    const el_tts_sentence_t *prev, const el_tts_sentence_t *next,
                    uint_fast64_t gen)
{
    job_t j;
    memset(&j, 0, sizeof j);
    j.ctx = c;
    j.gen = gen;
    j.sent = cur;

    E("run_job: processing sentence id=%llu text=\"%s\"", (unsigned long long)cur->id, cur->text);

    /* request body */
    sb_t body = {0};
    sb_str(&body, "{\"text\":");
    sb_json_str(&body, cur->text);
    sb_str(&body, ",\"model_id\":");
    sb_json_str(&body, c->model_id);
    if (c->stability >= 0 || c->similarity >= 0) {
        sb_str(&body, ",\"voice_settings\":{");
        bool comma = false;
        if (c->stability >= 0) {
            sb_str(&body, "\"stability\":");
            sb_fixed3(&body, c->stability);
            comma = true;
        }
        if (c->similarity >= 0) {
            if (comma) sb_str(&body, ",");
            sb_str(&body, "\"similarity_boost\":");
            sb_fixed3(&body, c->similarity);
        }
        sb_str(&body, "}");
    }
    if (c->use_context && prev) {
        sb_str(&body, ",\"previous_text\":");
        sb_json_str(&body, prev->text);
    }
    if (c->use_context && next) {
        sb_str(&body, ",\"next_text\":");
        sb_json_str(&body, next->text);
    }
    sb_str(&body, "}");

    if (body.oom) {
        E("run_job: JSON body builder encountered OOM");
        free(body.p);
        push_block(&j, NULL, 0, true, EL_TTS_ERR_NOMEM, "out of memory");
        return;
    }

    curl_easy_setopt(c->curl, CURLOPT_URL, c->url);
    curl_easy_setopt(c->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, body.p);
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDSIZE, (long)body.len);
    curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, &j);
    curl_easy_setopt(c->curl, CURLOPT_XFERINFODATA, &j);

    E("run_job: executing curl request to %s", c->url);
    CURLcode rc = curl_easy_perform(c->curl);
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &j.http_status);
    free(body.p);

    E("run_job: curl_easy_perform completed with rc=%d (%s), http_status=%ld", 
           rc, curl_easy_strerror(rc), j.http_status);

    if (job_aborted(&j)) {
        E("run_job: job was aborted after curl execution");
        return;                /* cancelled: emit nothing */
    }

    char msg[EL_TTS_ERROR_MSG_LEN];
    if (rc != CURLE_OK) {
        snprintf(msg, sizeof msg, "curl: %s", curl_easy_strerror(rc));
        E("run_job error: network error -> %s", msg);
        push_block(&j, NULL, 0, true, EL_TTS_ERR_NETWORK, msg);
    } else if (j.http_status != 200) {
        snprintf(msg, sizeof msg, "HTTP %ld: %s", j.http_status, j.errbody);
        E("run_job error: HTTP error -> %s", msg);
        push_block(&j, NULL, 0, true, EL_TTS_ERR_HTTP, msg);
    } else {
        E("run_job: success, flushing tail fill_len=%zu", j.fill_len);
        /* flush the tail (0..4095 bytes) as the final block */
        push_block(&j, j.fill, j.fill_len, true, EL_TTS_OK, NULL);
    }
}

/* ------------------------------------------------------------------------ */
/* Worker                                                                   */
/* ------------------------------------------------------------------------ */

// ... (keep prewarm, run_job, etc. as they are) ...

static void *worker_main(void *arg)
{
    ELEVENLABSTTS *c = arg;
    const el_tts_sentence_t *prev = NULL;

    E("worker_main: thread started");
    if (c->prewarm) prewarm(c);

    for (;;) {
        pthread_mutex_lock(&c->mu);
        while (!atomic_load(&c->shutdown) && !c->in_head && !c->input_closed) {
            pthread_cond_wait(&c->in_cv, &c->mu);
        }
        
        // If shutdown requested, break out completely
        if (atomic_load(&c->shutdown)) {
            E("worker_main: exiting due to shutdown");
            pthread_mutex_unlock(&c->mu);
            break;
        }

        // If input is closed but queue is empty, mark worker done and wait for reset/shutdown
        if (c->input_closed && !c->in_head) {
            c->worker_done = true;
            E("worker_main: input closed and queue empty, waiting for reset or shutdown");
            pthread_cond_broadcast(&c->out_not_empty);
            
            while (!atomic_load(&c->shutdown) && c->input_closed) {
                pthread_cond_wait(&c->in_cv, &c->mu);
            }
            
            if (atomic_load(&c->shutdown)) {
                pthread_mutex_unlock(&c->mu);
                break;
            }
            // Reset happened: clear worker_done and continue processing new items
            c->worker_done = false;
            sentence_unref(prev);
            prev = NULL;
            pthread_mutex_unlock(&c->mu);
            continue;
        }

        in_node_t *n = c->in_head;
        c->in_head = n->next;
        if (!c->in_head) c->in_tail = NULL;
        const el_tts_sentence_t *cur = n->s;
        free(n);
        const el_tts_sentence_t *next = c->in_head ? sentence_ref(c->in_head->s) : NULL;
        uint_fast64_t gen = atomic_load(&c->generation);
        pthread_mutex_unlock(&c->mu);

        run_job(c, cur, prev, next, gen);

        sentence_unref(next);
        sentence_unref(prev);
        if (atomic_load(&c->generation) == gen) {
            prev = cur;
        } else {
            E("worker_main: generation changed during job execution, discarding prev context");
            sentence_unref(cur);
            prev = NULL;
        }
    }
    sentence_unref(prev);

    pthread_mutex_lock(&c->mu);
    c->worker_done = true;
    E("worker_main: worker finished, broadcasting out_not_empty");
    pthread_cond_broadcast(&c->out_not_empty);
    pthread_mutex_unlock(&c->mu);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;
static void curl_global_once(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

void ELEVENLABSTTS_CONFIGDEFAULTS(ELEVENLABSTTSCONFIG *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->api_key               = NULL;
    cfg->voice_id              = NULL;
    cfg->model_id              = "eleven_flash_v2_5";
    cfg->base_url              = "https://api.elevenlabs.io";
    cfg->stability             = 0.5;
    cfg->similarity_boost      = 0.75;
    cfg->use_context_text      = false;
    cfg->prewarm               = true;
    cfg->output_queue_blocks = 64;
    cfg->connect_timeout_ms    = 5000;
}

ELEVENLABSTTS *ELEVENLABSTTS_CREATE(const ELEVENLABSTTSCONFIG *cfg)
{
    if (!cfg || !cfg->voice_id || !*cfg->voice_id) {
        E("el_tts_create: invalid configuration or missing voice_id");
        return NULL;
    }

    const char *key = cfg->api_key ? cfg->api_key : getenv("ELEVENLABS_API_KEY");
    if (!key || !*key) {
        E("el_tts_create: missing API key (config and environment empty)");
        return NULL;
    }

    pthread_once(&g_curl_once, curl_global_once);

    ELEVENLABSTTS *c = calloc(1, sizeof *c);
    if (!c) return NULL;

    c->api_key  = dupstr(key);
    c->voice_id = dupstr(cfg->voice_id);
    c->model_id = dupstr(cfg->model_id ? cfg->model_id : "eleven_flash_v2_5");
    c->base_url = dupstr(cfg->base_url ? cfg->base_url : "https://api.elevenlabs.io");
    c->stability   = cfg->stability;
    c->similarity  = cfg->similarity_boost;
    c->use_context = cfg->use_context_text;
    c->prewarm     = cfg->prewarm;
    c->connect_timeout_ms = cfg->connect_timeout_ms > 0 ? cfg->connect_timeout_ms : 5000;
    c->cap = cfg->output_queue_blocks ? cfg->output_queue_blocks : 64;
    atomic_init(&c->shutdown, false);
    atomic_init(&c->generation, 0);

    if (!c->api_key || !c->voice_id || !c->model_id || !c->base_url) goto fail;

    c->ring = calloc(c->cap, sizeof *c->ring);
    if (!c->ring) goto fail;

    c->curl = curl_easy_init();
    if (!c->curl) {
        E("el_tts_create: curl_easy_init failed");
        goto fail;
    }

    /* URL: {base}/v1/text-to-speech/{voice}/stream?output_format=pcm_16000 */
    {
        char *esc = curl_easy_escape(c->curl, c->voice_id, 0);
        if (!esc) goto fail;
        size_t n = strlen(c->base_url) + strlen(esc) + 96;
        c->url = malloc(n);
        if (!c->url) { curl_free(esc); goto fail; }
        snprintf(c->url, n, "%s/v1/text-to-speech/%s/stream?output_format=pcm_16000",
                 c->base_url, esc);
        curl_free(esc);
    }

    {
        size_t n = strlen(c->api_key) + 16;
        char *h = malloc(n);
        if (!h) goto fail;
        snprintf(h, n, "xi-api-key: %s", c->api_key);
        c->hdrs = curl_slist_append(NULL, h);
        free(h);
        if (c->hdrs) c->hdrs = curl_slist_append(c->hdrs, "Content-Type: application/json");
        if (!c->hdrs) goto fail;
    }

    curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, c->hdrs);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c->curl, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(c->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c->curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c->curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->curl, CURLOPT_CONNECTTIMEOUT_MS, c->connect_timeout_ms);
    curl_easy_setopt(c->curl, CURLOPT_FORBID_REUSE, 0L);

    {
        pthread_condattr_t ca;
        pthread_condattr_init(&ca);
        pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
        pthread_mutex_init(&c->mu, NULL);
        pthread_cond_init(&c->in_cv, &ca);
        pthread_cond_init(&c->out_not_empty, &ca);
        pthread_cond_init(&c->out_not_full, &ca);
        pthread_condattr_destroy(&ca);
    }

    if (pthread_create(&c->worker, NULL, worker_main, c) != 0) {
        E("el_tts_create: failed to create worker thread");
        pthread_mutex_destroy(&c->mu);
        pthread_cond_destroy(&c->in_cv);
        pthread_cond_destroy(&c->out_not_empty);
        pthread_cond_destroy(&c->out_not_full);
        goto fail;
    }
    c->worker_started = true;
    E("el_tts_create: successfully initialized context and started worker");
    return c;

fail:
    if (c->hdrs) curl_slist_free_all(c->hdrs);
    if (c->curl) curl_easy_cleanup(c->curl);
    free(c->ring); free(c->url);
    free(c->api_key); free(c->voice_id); free(c->model_id); free(c->base_url);
    free(c);
    return NULL;
}

void ELEVENLABSTTS_DESTROY(ELEVENLABSTTS *c)
{
    if (!c) return;
    E("el_tts_destroy: beginning teardown");

    pthread_mutex_lock(&c->mu);
    atomic_store(&c->shutdown, true);
    pthread_cond_broadcast(&c->in_cv);
    pthread_cond_broadcast(&c->out_not_full);
    pthread_cond_broadcast(&c->out_not_empty);
    pthread_mutex_unlock(&c->mu);

    if (c->worker_started) pthread_join(c->worker, NULL);

    for (in_node_t *n = c->in_head; n; ) {
        in_node_t *nx = n->next;
        sentence_unref(n->s);
        free(n);
        n = nx;
    }
    for (size_t i = 0; i < c->count; i++)
        sentence_unref(c->ring[(c->head + i) % c->cap].sentence);

    curl_slist_free_all(c->hdrs);
    curl_easy_cleanup(c->curl);
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->in_cv);
    pthread_cond_destroy(&c->out_not_empty);
    pthread_cond_destroy(&c->out_not_full);
    free(c->ring); free(c->url);
    free(c->api_key); free(c->voice_id); free(c->model_id); free(c->base_url);
    free(c);
    E("el_tts_destroy: teardown complete");
}

int ELEVENLABSTTS_SUBMIT(ELEVENLABSTTS *c, const char *text, uint64_t *out_id)
{
    if (!c || !text || !*text) return EL_TTS_ERR_INVALID;

    sentence_impl_t *s = sentence_new(text);
    in_node_t *n = malloc(sizeof *n);
    if (!s || !n) { free(s); free(n); return EL_TTS_ERR_NOMEM; }

    pthread_mutex_lock(&c->mu);
    if (c->input_closed || atomic_load(&c->shutdown)) {
        pthread_mutex_unlock(&c->mu);
        free(s); free(n);
        return EL_TTS_ERR_STATE;
    }
    s->pub.id = c->next_id++;
    n->s = &s->pub;
    n->next = NULL;
    if (c->in_tail) c->in_tail->next = n; else c->in_head = n;
    c->in_tail = n;
    if (out_id) *out_id = s->pub.id;
    
    E("el_tts_submit: submitted sentence id=%llu text=\"%s\"", (unsigned long long)s->pub.id, text);
    
    pthread_cond_signal(&c->in_cv);
    pthread_mutex_unlock(&c->mu);
    return EL_TTS_OK;
}

void ELEVENLABSTTS_CLOSEINPUT(ELEVENLABSTTS *c)
{
    if (!c) return;
    E("el_tts_close_input called");
    pthread_mutex_lock(&c->mu);
    c->input_closed = true;
    pthread_cond_broadcast(&c->in_cv);
    pthread_mutex_unlock(&c->mu);
}

void ELEVENLABSTTS_RESET(ELEVENLABSTTS *c)
{
    if (!c) return;
    E("el_tts_reset called");
    pthread_mutex_lock(&c->mu);

    // Increment generation to abort any active curl job immediately
    atomic_fetch_add(&c->generation, 1);

    // Flush any leftover nodes in the input queue
    for (in_node_t *n = c->in_head; n; ) {
        in_node_t *nx = n->next;
        sentence_unref(n->s);
        free(n);
        n = nx;
    }
    c->in_head = c->in_tail = NULL;

    // Flush any leftover blocks in the output ring buffer
    for (size_t i = 0; i < c->count; i++)
        sentence_unref(c->ring[(c->head + i) % c->cap].sentence);
    c->head = c->count = 0;

    // Re-open input and clear worker done state
    c->input_closed = false;
    c->worker_done = false;

    pthread_cond_broadcast(&c->out_not_full);
    pthread_cond_broadcast(&c->in_cv); // Wake up worker if it was waiting
    pthread_mutex_unlock(&c->mu);
}

void ELEVENLABSTTS_CANCEL(ELEVENLABSTTS *c)
{
    if (!c) return;
    E("el_tts_cancel called");
    pthread_mutex_lock(&c->mu);
    atomic_fetch_add(&c->generation, 1);

    for (in_node_t *n = c->in_head; n; ) {
        in_node_t *nx = n->next;
        sentence_unref(n->s);
        free(n);
        n = nx;
    }
    c->in_head = c->in_tail = NULL;

    for (size_t i = 0; i < c->count; i++)
        sentence_unref(c->ring[(c->head + i) % c->cap].sentence);
    c->head = c->count = 0;

    pthread_cond_broadcast(&c->out_not_full);    /* wake a worker blocked on a full ring */
    pthread_mutex_unlock(&c->mu);
}

int ELEVENLABSTTS_READ(ELEVENLABSTTS *c, ELEVENLABSTTSBLOCK *out, int timeout_ms)
{
    if (!c || !out) return EL_TTS_ERR_INVALID;

    struct timespec dl = {0};
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_MONOTONIC, &dl);
        dl.tv_sec  += timeout_ms / 1000;
        dl.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    }

    pthread_mutex_lock(&c->mu);
    while (c->count == 0) {
        if (c->worker_done || atomic_load(&c->shutdown)) {
            pthread_mutex_unlock(&c->mu);
            // E("el_tts_read: returning CLOSED (worker_done=%d, shutdown=%d)", c->worker_done, atomic_load(&c->shutdown));
            return EL_TTS_CLOSED;
        }
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&c->mu);
            return EL_TTS_TIMEOUT;
        }
        if (timeout_ms < 0) {
            pthread_cond_wait(&c->out_not_empty, &c->mu);
        } else if (pthread_cond_timedwait(&c->out_not_empty, &c->mu, &dl) == ETIMEDOUT
                    && c->count == 0 && !c->worker_done) {
            pthread_mutex_unlock(&c->mu);
            return EL_TTS_TIMEOUT;
        }
    }

    *out = c->ring[c->head];            /* ownership of the sentence ref moves to caller */
    c->head = (c->head + 1) % c->cap;
    c->count--;
    // E("el_tts_read: read block index=%u, size=%zu, remaining count=%zu", out->index, out->size, c->count);
    pthread_cond_signal(&c->out_not_full);
    pthread_mutex_unlock(&c->mu);
    return EL_TTS_OK;
}

void ELEVENLABSTTS_BLOCKRELREASE(ELEVENLABSTTSBLOCK *blk)
{
    if (!blk) return;
    sentence_unref(blk->sentence);
    blk->sentence = NULL;
}

const char *ELEVENLABSTTS_STRERROR(int st)
{
    switch (st) {
    case EL_TTS_OK:          return "ok";
    case EL_TTS_TIMEOUT:     return "timeout";
    case EL_TTS_CLOSED:      return "closed";
    case EL_TTS_ERR_INVALID: return "invalid argument";
    case EL_TTS_ERR_NOMEM:   return "out of memory";
    case EL_TTS_ERR_NETWORK: return "network error";
    case EL_TTS_ERR_HTTP:    return "http error";
    case EL_TTS_ERR_STATE:   return "input closed";
    default:                 return "unknown";
    }
}
