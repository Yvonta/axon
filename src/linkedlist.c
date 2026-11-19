#include "linkedlist.h"

#include <pthread.h>
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

// Pas insert aan om thread-safe te zijn
void LINKEDLIST_INSERT(LINKEDLIST *v, void* data)
{
    pthread_mutex_lock(&log_lock);
    LINKEDLIST_NODE *new_node = malloc(sizeof(LINKEDLIST_NODE));
    new_node->data = data;
    new_node->next = (void*)v->list;
    v->list = new_node;
    pthread_mutex_unlock(&log_lock);
}

// NIEUW: Haal het oudste bericht eruit (Pop van het einde)
void* LINKEDLIST_POPOLDEST(LINKEDLIST *v)
{
    pthread_mutex_lock(&log_lock);
    if (v->list == NULL) {
        pthread_mutex_unlock(&log_lock);
        return NULL;
    }

    LINKEDLIST_NODE *curr = v->list;
    LINKEDLIST_NODE *prev = NULL;

    // Loop naar het einde van de lijst
    while (curr->next != NULL) {
        prev = curr;
        curr = (LINKEDLIST_NODE*)curr->next;
    }

    void* data = curr->data;

    if (prev == NULL) {
        v->list = NULL; // Er was maar 1 item
    } else {
        prev->next = NULL; // Verwijder laatste item
    }

    free(curr);
    pthread_mutex_unlock(&log_lock);
    return data;
}

LINKEDLIST *LINKEDLIST_CREATE()
{
  LINKEDLIST *v = malloc(sizeof(LINKEDLIST));
  v->node = NULL;
  v->list = NULL;
  return v;
}

void LINKEDLIST_DESTROY(LINKEDLIST *v)
{
}

LINKEDLIST *LINKEDLIST_REVERT(LINKEDLIST *v)
{
	LINKEDLIST *n = (LINKEDLIST *)malloc(sizeof(LINKEDLIST));
	if(n == NULL)
	{
		return NULL;
	}
	LINKEDLIST_CREATE(n);
	
	LINKEDLIST_RESET(v);
	while(LINKEDLIST_WALK(v))
	{
		void *data = LINKEDLIST_GET(v);
		LINKEDLIST_INSERT(n, data);
	}
	LINKEDLIST_RESET(n);
	return n;
}


BOOL LINKEDLIST_WALK(LINKEDLIST *v)
{
	if(v->node == NULL)
		v->node = v->list;
	else
		v->node = (LINKEDLIST_NODE*)v->node->next;
	if(v->node == NULL)
		return FALSE;
    return TRUE;
}

void *LINKEDLIST_GET(LINKEDLIST *v)
{
	if(v->node == NULL)
		return NULL;
	return v->node->data;
}

void LINKEDLIST_RESET(LINKEDLIST *v)
{
	v->node = NULL;
}

void LIKEDLIST_FREEOBJECT(LINKEDLIST *v, void *object, void (*freeData)(void * object, void *data))
{
  while(v->list != NULL)
  {
    v->node = (LINKEDLIST_NODE*)v->list->next;
    if(v->list->data != NULL)
        (*freeData)(object, v->list->data);
    free(v->list);
    v->list = v->node;
  }
}

void LINKEDLIST_FREE(LINKEDLIST *v, void (*freeData)(void *data))
{
    if (v == NULL) return;

    pthread_mutex_lock(&log_lock);

    // Free all nodes and their data
    LINKEDLIST_NODE *curr = v->list;
    while (curr != NULL) {
        LINKEDLIST_NODE *next = (LINKEDLIST_NODE*)curr->next;
        if (curr->data != NULL && freeData != NULL) {
            (*freeData)(curr->data);
        }
        free(curr);
        curr = next;
    }
    
    // Clear list pointers
    v->list = NULL;
    v->node = NULL;

    pthread_mutex_unlock(&log_lock);

    // Finally, free the list structure itself to prevent memory leaks
    free(v);
}

void LINKEDLIST_FREEFUN(void *data)
{
    free(data);
}

