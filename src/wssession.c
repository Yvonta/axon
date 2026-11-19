#include "wssession.h"

WSSESSION *WSSESSION_CREATE(struct lws *wsi)
{
  WSSESSION *v = malloc(sizeof(WSSESSION));
  if(!v)
  {
    return NULL;
  }
  memset(v, 0, sizeof(WSSESSION));
  v->wsi = wsi;
  
  v->storage = malloc(RB_FRAMES * sizeof(BUSPACKAGE));
  if(!v->storage)
  {
    free(v);
    return NULL;
  }
  v->rb = PACKAGERING_CREATE(v->storage, RB_FRAMES);
  if(!v->rb)
  {
    free(v->storage);
    free(v);
    return NULL;
  }
  return v;
}

void WSSESSION_DESTROY(WSSESSION *v)
{
  if(v)
  {
    if(v->storage)
    {
      free(v->storage);
      v->storage = NULL;
    }
    if(v->rb)
    {
      PACKAGERING_DESTROY(v->rb);
      v->rb = NULL;
    }
    free(v);
  }
}
