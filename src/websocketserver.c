#include "websocketserver.h"

// Callback-functie die alle WebSocket-events afhandelt
static int WEBSOCKETSERVER_CALLBACK(struct lws *wsi, enum lws_callback_reasons reason,
                                   void *user, void *in, size_t len) 
{
    struct lws_context *context = lws_get_context(wsi);
    WEBSOCKETSERVER *v = (WEBSOCKETSERVER *)lws_context_user(context);

    switch (reason) 
    {
        case LWS_CALLBACK_ESTABLISHED:
        {   
          E("New client connected!");
          WSSESSIONMANAGER_NEW(v->sm, wsi);	
        } break;
        
        case LWS_CALLBACK_RECEIVE:
        { 
          WSSESSION *session = WSSESSIONMANAGER_GET(v->sm, wsi);
          if(!session)
          {
            break; 
          }	
        
          // Check for buffer overflow against fixed size package
          if(session->receivedbytes + len > sizeof(BUSPACKAGE)) 
          {
            E("Error: Package size overflow! Resetting buffer.");
            session->receivedbytes = 0;
            return -1;
          }

          // Copy incoming bytes directly into the session's package buffer pointer
          memcpy(((char*)&session->bp) + session->receivedbytes, in, len);
          session->receivedbytes += len;

          if(session->receivedbytes == sizeof(BUSPACKAGE)) 
          {
            if (v && v->funptr) 
            {
              // Pass the pointer correctly to the handler function
              v->funptr(session, wsi, &session->bp);
            }
            session->receivedbytes = 0; 
          }
        } break;

        case LWS_CALLBACK_CLOSED:
        { 
          E("Client disconnected.");
          WSSESSIONMANAGER_END(v->sm, wsi);	
          break;
        }
        
        case LWS_CALLBACK_SERVER_WRITEABLE:
        {   if(!v) 
            {
              break;
            }
            
            WSSESSION *session = WSSESSIONMANAGER_GET(v->sm, wsi);
            if(!session)
            {
              break; 
            }	
            
            size_t available = PACKAGERING_AVAILABLEREAD(session->rb);
            if(available > 0)
            {
               BUSPACKAGE bp;
               PACKAGERING_READ(session->rb, &bp);
               if(WEBSOCKETSERVER_SEND(wsi, &bp) < 0) 
               {
                  return -1; // Verbinding sluiten bij schrijffout
               }
               
               available = PACKAGERING_AVAILABLEREAD(session->rb);
               if(available > 0)
               {
                 lws_callback_on_writable(wsi);
               }  
            }
         } break;
        
        default:
            break;
    }

    return 0;
}

// Definieer de protocollen die de server ondersteunt
static struct lws_protocols protocols[] = {
    {
        "persona-protocol",       // Naam van het WebSocket sub-protocol
        WEBSOCKETSERVER_CALLBACK,    // De callback-functie hiervoor
        sizeof(BUSPACKAGE),       // Grootte van optionele per-session data
        8192,                       // Maximale pakketgrootte / ontvangstbuffer
        0, NULL, 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } /* Einde van de lijst (terminator) */
};

WEBSOCKETSERVER *WEBSOCKETSERVER_CREATE(int port, void (*funptr)(WSSESSION *session, struct lws *wsi, BUSPACKAGE *bp))
{
    WEBSOCKETSERVER *v = malloc(sizeof(WEBSOCKETSERVER));
    if (!v)
    {
      return NULL; // Safety check
    }
    
    v->sm = WSSESSIONMANAGER_CREATE();
    v->funptr = funptr;
    
    struct lws_context_creation_info info;

    // Initialiseer de context configuratie
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.user = v;
    
    // --- READ CERTIFICATES FROM ENVIRONMENT VARIABLES ---
    char *cert_path = getenv("WSS_CERT_PATH");
    char *key_path = getenv("WSS_KEY_PATH");

    if (cert_path && key_path) 
    {
        info.ssl_cert_filepath = cert_path;
        info.ssl_private_key_filepath = key_path;
        
        // This option tells libwebsockets to initialize its SSL/TLS engine
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        
        // Optional: info.options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;
        
        E("[WSS] Launching securely using environment variables.");
        E("[WSS] Cert: %s\n", cert_path);
        E("[WSS] Key:  %s\n", key_path);
    } 
    else 
    {
        E("Missing WSS_CERT_PATH or WSS_KEY_PATH environment vars. Falling back to unsecure WS.");
    }
    // ----------------------------------------------------    
    

    // Maak de libwebsockets context aan
    v->context = lws_create_context(&info);
    if (!v->context) 
    {
        E("Initialisatie van libwebsockets context mislukt\n");
        return NULL;
    }

    return v;
}

void WEBSOCKETSERVER_DESTROY(WEBSOCKETSERVER *v)
{
  // Ruim netjes op
  if(v)
  {
  	if(v->sm)
  	{
  	  WSSESSIONMANAGER_DESTROY(v->sm);
  	  v->sm = NULL;
  	}
  	lws_context_destroy(v->context);
  	
  	free(v);
  }
}

void WEBSOCKETSERVER_TICK(WEBSOCKETSERVER *v)
{
  lws_service(v->context, -1);
}

int WEBSOCKETSERVER_SEND(struct lws *wsi, BUSPACKAGE *bp)
{
    if (!wsi || !bp) return -1;

    // 1. Maak een buffer aan met ruimte voor LWS_PRE headers + onze BUSPACKAGE
    unsigned char send_buf[LWS_PRE + sizeof(BUSPACKAGE)];

    // 2. Kopieer het pakket direct ná de LWS_PRE ruimte in de buffer
    memcpy(&send_buf[LWS_PRE], bp, sizeof(BUSPACKAGE));

    // 3. Stuur het weg als een binair WebSocket-pakket
    int n = lws_write(wsi, &send_buf[LWS_PRE], sizeof(BUSPACKAGE), LWS_WRITE_BINARY);

    if (n < 0) {
        E("Fout bij het verzenden van pakket via WebSocket!");
        return -1;
    }

    return n; // Geeft het aantal verzonden bytes terug
}

void WEBSOCKETSERVER_SENDALL(WEBSOCKETSERVER *v, BUSPACKAGE *bp)
{
  if (!v || !bp) return;


  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
     if(v->sm->sessions[i] != NULL)
     {
       WEBSOCKETSERVER_SEND(v->sm->sessions[i]->wsi, bp);     
     }
  }
}

int WEBSOCKETSERVER_SENDCHAT(WEBSOCKETSERVER *v, struct lws *wsi, char *msg)
{
    if (!wsi || !msg) return -1;

    WSSESSION *session = WSSESSIONMANAGER_GET(v->sm, wsi);
    if(!session)
    {
      return -1; 
    }	

    size_t available = PACKAGERING_AVAILABLEWRITE(session->rb);
    if(available <= 0)
    {
      return -1;    
    }

    BUSPACKAGE bp;
    
    memset(&bp, 0, sizeof(BUSPACKAGE));
    
    bp.fixed = 0xDEADBEEF;
    bp.type = 1;
    bp.payloadlen = strlen(msg);

    if(bp.payloadlen > sizeof(bp.payload))
    {
      return -1;
    }  
    memcpy(bp.payload, msg, bp.payloadlen);

    PACKAGERING_WRITE(session->rb, &bp);
    
    lws_callback_on_writable(wsi);
    lws_cancel_service(v->context);

    return sizeof(BUSPACKAGE);
}

void WEBSOCKETSERVER_SENDCHATALL(WEBSOCKETSERVER *v, char *msg)
{
    if (!v || !msg) return;

    BUSPACKAGE *bp = BUSPACKAGE_CREATE();
    
    if(!bp)
    {
    	return;
    }
    
    memset(bp, 0, sizeof(BUSPACKAGE));
    bp->fixed = 0xDEADBEEF;
    bp->type = 1;
    bp->payloadlen = strlen(msg);
    
    if(bp->payloadlen > sizeof(bp->payload))
    {
      BUSPACKAGE_DESTROY(bp);
      return;
    }  
    memcpy(bp->payload, msg, bp->payloadlen);

    WEBSOCKETSERVER_SENDALL(v, bp);

    BUSPACKAGE_DESTROY(bp);
}


int WEBSOCKETSERVER_SENDPCM(WEBSOCKETSERVER *v, struct lws *wsi, char *pcm, int len)
{
    if(!wsi || !pcm) 
    {
      return -1;
    }

    WSSESSION *session = WSSESSIONMANAGER_GET(v->sm, wsi);
    if(!session)
    {
      return -1; 
    }	

    size_t available = PACKAGERING_AVAILABLEWRITE(session->rb);
    if(available <= 0)
    {
      return -1;    
    }

    BUSPACKAGE bp;
      
    memset(&bp, 0, sizeof(BUSPACKAGE));
    bp.fixed = 0xDEADBEEF;
    bp.type = 2;
    bp.payloadlen = len;

    if(bp.payloadlen > sizeof(bp.payload))
    {
      E("Error: Payload to big!");
      return -1;
    }  
    memcpy(bp.payload, pcm, bp.payloadlen);
     
    PACKAGERING_WRITE(session->rb, &bp);
    
    lws_callback_on_writable(wsi);
    lws_cancel_service(v->context);
    
    return sizeof(BUSPACKAGE);
}

void WEBSOCKETSERVER_SENDPCMALL(WEBSOCKETSERVER *v, char *pcm, int len)
{
  if (!v || !pcm) return;


  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
     if(v->sm->sessions[i] != NULL)
     {
       WEBSOCKETSERVER_SENDPCM(v, v->sm->sessions[i]->wsi, pcm, len);
     }
  }
}


void WEBSOCKETSERVER_SENDPCMOTHERS(WEBSOCKETSERVER *v, struct lws *wsi, char *pcm, int len)
{
  if (!v || !pcm) return;


  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
     if(v->sm->sessions[i] != NULL && wsi != v->sm->sessions[i]->wsi)
     {
       WEBSOCKETSERVER_SENDPCM(v, v->sm->sessions[i]->wsi, pcm, len);     
     }
  }
}


