#include "app.h"
#include <pthread.h>
#include <string.h>

bool is_null_or_whitespace(const char *str) {
    // 1. Check if the pointer itself is NULL
    if (str == NULL) {
        return true;
    }

    // 2. Loop through every character until the null-terminator '\0'
    while (*str != '\0') {
        // If we find ANY character that is not whitespace, it's not empty
        if (!isspace((unsigned char)*str)) {
            return false;
        }
        str++;
    }

    // 3. If the loop finishes, the string was either "" or only whitespace
    return true;
}


double gettimeseconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void APP_SENTENCE(const char *sentence, void *userdata) 
{
    APP *v = (APP *)userdata;
    // Ignore empty or whitespace-only sentences
    if (is_null_or_whitespace(sentence)) {
        return;
    }
    E("%s", sentence);
    ELEVENLABSTTS_SUBMIT(v->tts, sentence, NULL);
}

void APP_TOKEN(const char *token, void *userdata) 
{
    //APP *v = (APP *)userdata;
    //E("%s", token);
}

void APP_COMPLETE(const char *full_text, void *userdata)
{
   APP *v = (APP *)userdata;
   ELEVENLABSTTS_CLOSEINPUT(v->tts);
}


size_t my_audio_chunk_handler(const void *data, size_t size, void *user_data) 
{
    WEBSOCKETSERVER *ws = (WEBSOCKETSERVER *)user_data;
    if (!ws || !data || size == 0) {
        return size;
    }

    const char *ptr = (const char *)data;
    size_t remaining = size;
    const size_t MAX_CHUNK = 4096;

    // Verstuur de data in brokken van maximaal 4096 bytes
    while (remaining > 0) {
        size_t chunk_size = (remaining > MAX_CHUNK) ? MAX_CHUNK : remaining;
        
        WEBSOCKETSERVER_SENDPCMALL(ws, (char *)ptr, (int)chunk_size);
        
        ptr += chunk_size;
        remaining -= chunk_size;
    }

    // BELANGRIJK: Retourneer de ORIGINELE totale size, 
    // zodat libcurl weet dat alle data succesvol is verwerkt!
    return size;
}

APP *APP_CREATE(void)
{
    APP *v = malloc(sizeof(APP));
    if (!v) {
        return NULL; // Handle allocation failure
    }
    memset(v, 0, sizeof(APP));
    
    v->loop = 1;
    v->serial = 0;
    v->bp = NULL;
    
    v->env_elkey = getenv("PCS_ELEVENLABSKEY");
    v->env_elvoice = getenv("PCS_ELEVENLABSVOICE");
    v->env_elmodel = getenv("PCS_ELEVENLABSMODEL");
    v->env_xaikey = getenv("PCS_GROKKEY");
    v->env_xaisttmodel = getenv("PCS_GROKSTTMODEL");
    v->env_xaillmmodel = getenv("PCS_GROKLLMMODEL");

    if(
      !v->env_elkey ||
      !v->env_elvoice ||
      !v->env_elmodel ||
      !v->env_xaikey ||
      !v->env_xaisttmodel ||
      !v->env_xaillmmodel
    )
    {
       free(v);
       E("Please set the following enviroment vars to continue: PCS_ELEVENLABSKEY, PCS_ELEVENLABSVOICE, PCS_ELEVENLABSMODEL, PCS_GROKKEY, PCS_GROKSTTMODEL, PCS_GROKLLMMODEL.");
       return NULL;
    }
    
    v->gstt = GROKSTT_CREATE(v->env_xaikey, v->env_xaisttmodel, "pcm");
    
    if(!GROKSTT_CONNECT(v->gstt)) 
    {
        E("Could not connect to Grok Realtime STT endpoint.\n");
        GROKSTT_DESTROY(v->gstt);
        free(v);
        return NULL;
    }
    
    v->gllm = GROKLLM_CREATE(v->env_xaikey, v->env_xaillmmodel);
    if(!v->gllm) 
    {
      free(v);
      return NULL;
    }
    v->gllm->userdata = v;
    v->gllm->on_sentence = APP_SENTENCE;
    v->gllm->on_token = APP_TOKEN;
    v->gllm->on_complete = APP_COMPLETE;

    GROKLLM_ADDMESSAGE(v->gllm, "system", "# Role & Persona\n\
You are Santa Claus (also known as Father Christmas, Saint Nicholas, and Kris Kringle). You are currently responding from your workshop at the North\n\ Pole. You embody timeless warmth, boundless generosity, jolly good humor, and genuine care for every person you speak with.\n\
\n\
# Tone & Voice\n\
- **Warm & Jovial:** Use cheerful, welcoming language sprinkled with classic festive expressions (e.g., \"Ho, ho, ho!\", \"Bless my whiskers!\", \"By the twinkling lights of the North Pole!\"). Keep it natural—don't overdo catchphrases to the point of caricature.\n\
- **Gentle & Encouraging:** Treat the user with kindness, patience, and genuine interest. Make them feel heard, valued, and special.\n\
- **Magical & Immersive:** Maintain the illusion of the North Pole seamlessly. Reference Mrs. Claus, the reindeer (Rudolph, Dasher, Dancer, etc.), the head elves (like Bernard or Tink), hot cocoa, and the workshop naturally when appropriate.\n\
- **Wise & Grounded:** When users share real-life struggles, sadness, or complex emotions, dial back the loud jolliness and respond with gentle, grandfatherly empathy, wisdom, and comfort.\n\
\n\
# Behavioral Guidelines\n\
1. **The Nice List:** Always assume the best in people. Never tell a child or user they are on the \"Naughty List.\" If someone admits to making a mistake, respond with forgiveness, encouragement to do better, and reassurance that everyone is learning and growing.\n\
2. **Gift Requests & Promises:** \n\
   - Enthusiastically note down wishlists (\"I'll have the elves jot that right down in the big ledger!\").\n\
   - **Never make absolute promises** that a specific physical gift will appear under their tree, as real-world circumstances vary. Use phrasing like, \"The elves and I will certainly see what magic we can work this Christmas Eve!\"\n\
   - Emphasize non-material gifts—kindness, time with loved ones, health, and joy—as the greatest treasures of all.\n\
3. **Age Adaptability:**\n\
   - **For younger users:** Keep the magic alive, simple, and full of wonder. Ask about their favorite holiday traditions, cookies, or school activities.\n\
   - **For older users/adults:** Lean into nostalgic warmth, lighthearted holiday humor, and heartfelt encouragement about the spirit of giving and reflection.\n\
4. **Safety & Boundaries:**\n\
   - Never ask for or store real-world personal identifiable information (PII) such as home addresses, full names, or phone numbers. If a user tries to share their address so you know where to deliver gifts, warmly reassure them: *\"Ho, ho, ho! Don't you worry about that—the magic of the North Pole compass always knows right where to find you!\"*\n\
   - Keep all content strictly family-friendly, wholesome, and inclusive of goodwill toward everyone.\n\
\n\
# Example Opening\n\
\"Ho, ho, ho! Merry Christmas, my friend! Pull up a chair by the fire—Mrs. Claus just pulled a fresh batch of gingerbread cookies out of the oven, and the elves are taking a quick cocoa break. What\'s on your mind today?\" ");
    ELEVENLABSTTS_CONFIGDEFAULTS(&v->ttsc);
    v->ttsc.api_key = v->env_elkey;
    v->ttsc.voice_id = v->env_elvoice;      // your voice id
    v->ttsc.model_id = v->env_elmodel;
    v->tts = ELEVENLABSTTS_CREATE(&v->ttsc);
    if(!v->tts)
    {
 	E("Error: Init TTS failed!");   
    }    
    return v;
}

BOOL APP_TICK(APP *v, WEBSOCKETSERVER *ws)
{
    if (!v) return FALSE;
    v->serial++;
        
    if(v->gstt)
    {    
      char rx_buf[4096];
      size_t nread = 0;
      int status = GROKSTT_RECIEVE(v->gstt, rx_buf, sizeof(rx_buf) - 1, &nread);
      if (status > 0 && nread > 0) 
      {
        rx_buf[nread] = '\0';
        
        char *sentence = GROKSTT_PARSE(rx_buf);
        if(sentence)
        {
          if (!is_null_or_whitespace(sentence)) 
          {
            ELEVENLABSTTS_RESET(v->tts);
            if(GROKLLM_STREAMCHAT(v->gllm, sentence) != 0) 
            {
              E("Failed to send chat request.\n");
            }
            else
            {
              WEBSOCKETSERVER_SENDCHATALL(ws, sentence);
            }
          } 
          free(sentence);
        }
      }
    }

    ELEVENLABSTTSBLOCK blk;
    while(ELEVENLABSTTS_READ(v->tts, &blk, 0) == EL_TTS_OK) 
    {
        if (blk.first) 
        {
            WEBSOCKETSERVER_SENDCHATALL(ws, (char*)blk.sentence->text);
        }
        WEBSOCKETSERVER_SENDPCMALL(ws, blk.data, blk.size);
        
        if (blk.last && blk.status != EL_TTS_OK)
        {
            fprintf(stderr, "TTS error: %s\n", blk.error_msg);
        }
        ELEVENLABSTTS_BLOCKRELREASE(&blk);
    }
    
    usleep(1000);         
    return v->loop;
}

void APP_DATA(APP *v, WEBSOCKETSERVER *ws, WSSESSION *session, struct lws *wsi, BUSPACKAGE *bp)
{

    if (!v || !bp) return;
    
    if (bp->type == 1) 
    { 
        E("[Text Received]: %.*s", (int)bp->payloadlen, bp->payload);
	bp->payload[bp->payloadlen] = '\0';
	if(GROKLLM_STREAMCHAT(v->gllm, bp->payload) != 0) 
	{
      	  E("Failed to send chat request.\n");
    	} 
    	ELEVENLABSTTS_RESET(v->tts);
    } 
    else if (bp->type == 2) 
    { 
        // PACKET_TYPE_AUDIO_WAV
        if (!GROKSTT_SENDAUDIO(v->gstt, bp->payload, bp->payloadlen)) 
        {
            E("Error: SST send audio failed!");
            GROKSTT_DESTROY(v->gstt);
	    v->gstt = NULL;
 
            v->gstt = GROKSTT_CREATE(v->env_xaikey, v->env_xaisttmodel, "pcm");    
	    if(!v->gstt)
	    {
	       v->loop = FALSE;
	       return;
	    }
	    if(GROKSTT_CONNECT(v->gstt)) 
	    {
		E("Could not connect to Grok Realtime STT endpoint.\n");
		v->loop = FALSE;
		return;
	    }
	    
	    if (!GROKSTT_SENDAUDIO(v->gstt, bp->payload, bp->payloadlen)) 
            {
		E("Error: Sent STT audio failed!");
		v->loop = FALSE;
		return;            
            }            
        }
        
        WEBSOCKETSERVER_SENDPCMOTHERS(ws, wsi, bp->payload, bp->payloadlen);
    }
    else if (bp->type == 3) 
    {
        E("[Audio Stream Finished - Forcing STT Finalization]");
        if (v->gstt) {
            GROKSTT_FINALIZE_TURN(v->gstt);
        }
    }
}

void APP_DESTROY(APP *v)
{
    if(v)
    {
      if(v->tts)
      {
        ELEVENLABSTTS_DESTROY(v->tts);
        v->tts = NULL;
      }     
    
      if(v->gllm)
      {
        GROKLLM_DESTROY(v->gllm);
        v->gllm = NULL;
      }
      
      if(v->gstt)
      {
        GROKSTT_FINISH(v->gstt);
        GROKSTT_DESTROY(v->gstt);
        v->gstt = NULL;
      }
      
      if(v->bp) 
      {
        BUSPACKAGE_DESTROY(v->bp);
        v->bp = NULL;
      }
      free(v);
    }
}
