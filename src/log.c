#include "log.h"

typedef struct 
{
	FILE *file;
	FILE *out;
	FILE *err;
	int (*logfuncptr)(char *);
	LINKEDLIST *lines;
	unsigned char *field;
	int fieldwidth;
	int fieldheight;
	int fieldsize;
} LOG;

LOG *LOG_GET() 
{
	static LOG data;
	return &data;
}

CRESULT LOG_MSG(const char *fmt, ...) 
{
    char *buffer = malloc(sizeof(char) * 2048);

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, 2048, fmt, arg);
    va_end(arg);

    // Voeg toe aan de interne gelinkte lijst
    if(LOG_GET()->lines)
    {
    	LINKEDLIST_INSERT(LOG_GET()->lines, (void*)buffer);
    }
    
    if(LOG_GET()->logfuncptr) 
    {
        (*LOG_GET()->logfuncptr)(buffer);
    }

    // Optioneel: schrijf nog steeds naar file
    if(LOG_GET()->file) 
    {
        fprintf(LOG_GET()->file, "%s\n", buffer);
        fflush(LOG_GET()->file);
    }
    
    // free is not stored in mem
    if(!LOG_GET()->lines)
    {
    	free(buffer);
    }

    return CSUCCESS;
}

CRESULT LOG_POLL(char *out_buffer, int max_size) 
{
    if(!LOG_GET()->lines)
    {
    	return CFAILED;
    }


    char *msg = (char *)LINKEDLIST_POPOLDEST(LOG_GET()->lines);
    if (msg) 
    {
        strncpy(out_buffer, msg, max_size - 1);
        out_buffer[max_size - 1] = '\0';
        free(msg);
        return CSUCCESS;
    }
    return CFAILED;
}


CRESULT LOG_CREATE(BOOL write, BOOL mem, BOOL std, int (*logfuncptr)(char *)) {
    LOG_GET()->file = NULL;
    LOG_GET()->logfuncptr = NULL;
    LOG_GET()->out = NULL;	
    LOG_GET()->err = NULL;
    LOG_GET()->field = NULL;
    LOG_GET()->lines = NULL;
    
    if(mem)
    {
    	LOG_GET()->lines = LINKEDLIST_CREATE();
    } 	

    if(logfuncptr) 
    {
       LOG_GET()->logfuncptr = logfuncptr;
    }

    if(std)
    {
       LOG_GET()->out = freopen("stdout.txt", "a", stdout);
       LOG_GET()->err = freopen("stderr.txt", "a", stderr);
    }

    if(write) 
    {
        if((LOG_GET()->file = fopen("log.txt", "a"))==NULL) 
        {
            return CFAILED;
        }
    }

    LOG_MSG("-= START =-");

    return CSUCCESS;
}

void LOG_DESTROY() 
{
    LOG_MSG("-= STOP =-");

    if(LOG_GET()->lines)
    {
      LINKEDLIST_FREE(LOG_GET()->lines, LINKEDLIST_FREEFUN);
      LINKEDLIST_DESTROY(LOG_GET()->lines);
      LOG_GET()->lines = NULL;
    }
    
    if(LOG_GET()->file)
    { 
 	fclose(LOG_GET()->file);
    }
    
    if(LOG_GET()->out)
    { 
	fclose(LOG_GET()->out);
    }
    
    if(LOG_GET()->err)
    { 
        fclose(LOG_GET()->err);
    }
}


