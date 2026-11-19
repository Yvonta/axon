/*
 * elevenlabs_tts.h - Low-latency streaming ElevenLabs text-to-speech client.
 *
 * - Sentences go into a queue and are synthesized one after another.
 * - Audio is raw PCM: signed 16-bit little-endian, mono, 16000 Hz (pcm_16000).
 * - Audio is delivered in blocks of EL_TTS_BLOCK_SIZE (4096) bytes
 *   (= 128 ms of audio). Every block carries a pointer to the sentence it
 *   belongs to, so you can drive subtitles from it.
 * - Streaming starts with the first bytes the server sends (HTTP chunked
 *   response); the TLS connection is kept alive between sentences and is
 *   pre-warmed at creation to shave off connect/handshake latency.
 *
 * Dependencies: libcurl, pthreads, C11 (stdatomic).
 * Build:  gcc -std=c11 -O2 -pthread -c elevenlabs_tts.c
 *         link your program with: -lcurl -pthread
 *
 * Minimal usage:
 *
 *   el_tts_config_t cfg;
 *   el_tts_config_default(&cfg);
 *   cfg.voice_id = "21m00Tcm4TlvDq8ikWAM";      // your voice id
 *   // cfg.api_key == NULL -> taken from env ELEVENLABS_API_KEY
 *   el_tts_t *tts = el_tts_create(&cfg);
 *
 *   el_tts_submit(tts, "Hello there.", NULL);
 *   el_tts_submit(tts, "This plays right after the first sentence.", NULL);
 *   el_tts_close_input(tts);                    // optional: lets read() end
 *
 *   el_tts_block_t blk;
 *   while (el_tts_read(tts, &blk, -1) == EL_TTS_OK) {
 *       if (blk.first) show_subtitle(blk.sentence->text);
 *       play_pcm(blk.data, blk.size);           // blk.size <= 4096
 *       if (blk.last && blk.status != EL_TTS_OK)
 *           fprintf(stderr, "TTS error: %s\n", blk.error_msg);
 *       el_tts_block_release(&blk);
 *   }
 *   el_tts_destroy(tts);
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "commons.h"
#include "log.h"

#define EL_TTS_BLOCK_SIZE     4096
#define EL_TTS_SAMPLE_RATE    16000   /* Hz, mono, int16 little-endian */
#define EL_TTS_ERROR_MSG_LEN  160

typedef enum {
    EL_TTS_OK          = 0,   /* success / block returned                   */
    EL_TTS_TIMEOUT     = 1,   /* el_tts_read(): timeout, nothing available  */
    EL_TTS_CLOSED      = 2,   /* el_tts_read(): input closed and all drained */
    EL_TTS_ERR_INVALID = -1,
    EL_TTS_ERR_NOMEM   = -2,
    EL_TTS_ERR_NETWORK = -3,  /* curl / transport error                     */
    EL_TTS_ERR_HTTP    = -4,  /* server answered with non-200               */
    EL_TTS_ERR_STATE   = -5   /* e.g. submit after el_tts_close_input()     */
} el_tts_status_t;

/* A queued sentence. Reference counted internally; it stays valid for as long
 * as any block that points to it has not been released. Read-only. */
typedef struct el_tts_sentence {
    uint64_t    id;        /* running id, starts at 0 */
    const char *text;      /* original sentence (UTF-8, NUL terminated) */
    size_t      text_len;
} el_tts_sentence_t;

/* One output block. Always call el_tts_block_release() when done with it. */
typedef struct {
    uint8_t                   data[EL_TTS_BLOCK_SIZE]; /* PCM s16le mono 16 kHz */
    size_t                    size;       /* valid bytes in data. == 4096 except
                                             for the final block of a sentence,
                                             which may be shorter or even 0   */
    const el_tts_sentence_t  *sentence;   /* the original sentence             */
    uint32_t                  index;      /* block number within the sentence  */
    bool                      first;      /* first block of this sentence      */
    bool                      last;       /* final block of this sentence      */
    el_tts_status_t           status;     /* only meaningful if last: EL_TTS_OK
                                             or the error that ended the sentence */
    long                      http_status;/* only meaningful if last          */
    char                      error_msg[EL_TTS_ERROR_MSG_LEN]; /* if status != OK */
} ELEVENLABSTTSBLOCK;

typedef struct {
    const char *api_key;            /* NULL -> getenv("ELEVENLABS_API_KEY")        */
    const char *voice_id;           /* required                                   */
    const char *model_id;           /* default "eleven_flash_v2_5" (lowest latency) */
    const char *base_url;           /* default "https://api.elevenlabs.io"        */
    double      stability;          /* 0..1, default 0.5, <0 = let server decide  */
    double      similarity_boost;   /* 0..1, default 0.75, <0 = let server decide */
    bool        use_context_text;   /* send previous_text / next_text for smoother
                                       prosody across sentences (default false;
                                       not supported by every model)              */
    bool        prewarm;            /* open the TLS connection at create (default true) */
    size_t      output_queue_blocks;/* max blocks buffered ahead (default 64 =
                                       ~8.2 s of audio). The downloader blocks
                                       (back-pressure) when this is full.        */
    long        connect_timeout_ms; /* default 5000                               */
} ELEVENLABSTTSCONFIG;

typedef struct elevenlabstts ELEVENLABSTTS;

/* Fill cfg with defaults. Always call this before editing a config. */
void ELEVENLABSTTS_CONFIGDEFAULTS(ELEVENLABSTTSCONFIG *cfg);

/* Create the client and start its worker thread. Returns NULL on failure
 * (missing voice id / api key, out of memory, curl init failure).
 * All strings in cfg are copied. */
ELEVENLABSTTS *ELEVENLABSTTS_CREATE(const ELEVENLABSTTSCONFIG *cfg);

/* Stop everything and free the client. Blocks that you still hold stay valid
 * until you release them. Safe to call while other threads are NOT using it. */
void ELEVENLABSTTS_DESTROY(ELEVENLABSTTS *tts);

/* Queue a sentence (text is copied). Thread-safe. Returns EL_TTS_OK, or
 * EL_TTS_ERR_STATE if the input was closed. out_id may be NULL. */
int ELEVENLABSTTS_SUBMIT(ELEVENLABSTTS *tts, const char *sentence, uint64_t *out_id);

/* Declare that no more sentences will be submitted. After the queue is fully
 * played out, el_tts_read() returns EL_TTS_CLOSED. */
void ELEVENLABSTTS_CLOSEINPUT(ELEVENLABSTTS *tts);

/* Barge-in: drop all queued sentences, drop all buffered audio and abort the
 * sentence currently downloading. New sentences can be submitted right away.
 * Blocks you already hold are unaffected. Thread-safe. */
void ELEVENLABSTTS_RESET(ELEVENLABSTTS *tts);

/* Fetch the next audio block (copied into *out).
 *   timeout_ms < 0  : wait forever
 *   timeout_ms == 0 : poll
 *   timeout_ms > 0  : wait up to that long
 * Returns EL_TTS_OK, EL_TTS_TIMEOUT, or EL_TTS_CLOSED.
 * On EL_TTS_OK you must call el_tts_block_release(out). Thread-safe. */
int ELEVENLABSTTS_READ(ELEVENLABSTTS *tts, ELEVENLABSTTSBLOCK *out, int timeout_ms);

/* Release the sentence reference held by a block. Idempotent. */
void ELEVENLABSTTS_BLOCKRELREASE(ELEVENLABSTTSBLOCK *blk);

const char *ELEVENLABSTTS_STRERROR(int status);

void ELEVENLABSTTS_RESET(ELEVENLABSTTS *c);
