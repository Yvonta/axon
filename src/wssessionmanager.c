#include "wssessionmanager.h"

WSSESSIONMANAGER *WSSESSIONMANAGER_CREATE()
{
  WSSESSIONMANAGER *v = malloc(sizeof(WSSESSIONMANAGER));
  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
  	v->sessions[i] = NULL;
  }

}

WSSESSION *WSSESSIONMANAGER_NEW(WSSESSIONMANAGER *v, struct lws *wsi)
{
  if(!wsi || !v)
  {
    return NULL;
  }
    
  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
    if(v && v->sessions[i] == NULL)
    {
	WSSESSION *session = WSSESSION_CREATE(wsi);
	if(!v || !session)
	{
		return NULL;
	}
	v->sessions[i] = session;
	return session;
    }
  }
  return NULL;
}

WSSESSION *WSSESSIONMANAGER_GET(WSSESSIONMANAGER *v, struct lws *wsi)
{
  if(!wsi || !v)
  {
    return NULL;
  }
  
  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
    if(v && v->sessions[i] != NULL)
    {
      if(v && v->sessions[i]->wsi == wsi)
      {
	  return v->sessions[i];     	
      }
    }
  }

  return NULL;
}

void WSSESSIONMANAGER_END(WSSESSIONMANAGER *v, struct lws *wsi)
{
  if(!wsi || !v)
  {
    return;
  }
  
  for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
  {
    if(v && v->sessions[i] != NULL)
    {
      if(v && v->sessions[i]->wsi == wsi)
      {
	  WSSESSION_DESTROY(v->sessions[i]);
	  v->sessions[i] = NULL;
	  
	  return;       	
      }
    }
  }
  
  return;
}


void WSSESSIONMANAGER_DESTROY(WSSESSIONMANAGER *v)
{
  if(v)
  {
    
    for(int i = 0; i < WSSESSIONMANAGER_MAXSESSIONS; i++)
    {
      if(v && v->sessions[i] != NULL)
      {
         free(v->sessions[i]);
         v->sessions[i] = NULL;   
      }
    }
    
    free(v);
  }
}

