#include "config.h"
#include "app.h"
#include "websocketserver.h"

static BOOL loop = TRUE;
static APP *app = NULL;
static WEBSOCKETSERVER *ws = NULL;

int logfun(char *msg)
{
  struct timespec ts;

  clock_gettime(CLOCK_REALTIME, &ts);

  struct tm *tminfo = localtime(&ts.tv_sec);

  char timebuf[32];
  strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S", tminfo);

  int subsec = (int)(ts.tv_nsec / 100000);

  printf("%s:%04d] ", timebuf, subsec);
  
  printf("%s\n", msg);
}

static void siginthandler(int sig) 
{
  loop = FALSE;
}

void newdata(WSSESSION *session, struct lws *wsi, BUSPACKAGE *bp)
{
  APP_DATA(app, ws, session, wsi, bp);
}

int main()
{ 
  LOG_CREATE(FALSE, FALSE, FALSE, logfun);
  
  app = APP_CREATE();
  if(!app)
  {
    E("ERROR: Creating app failed!");
  }

  ws = WEBSOCKETSERVER_CREATE(8079, newdata); 

  // Vang Ctrl+C af
  signal(SIGINT, siginthandler);
  
  while(loop && APP_TICK(app, ws))
  {
    WEBSOCKETSERVER_TICK(ws);
  }
  
  WEBSOCKETSERVER_DESTROY(ws);
  ws = NULL;
  
  APP_DESTROY(app);
  app = NULL;
  
  LOG_DESTROY();
}
