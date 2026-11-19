#include "buspackage.h"

BOOL BUSPACKAGE_TEST(BUSPACKAGE *bp)
{
  if(!bp)
  {
    return FALSE;
  }	
  
  if (bp->fixed == 0xDEADBEEF)
  {
    return TRUE;		
  }
  
  return FALSE;  
} 	

BUSPACKAGE *BUSPACKAGE_CREATE()
{
  BUSPACKAGE *v = malloc(sizeof(BUSPACKAGE));
  memset(v, 0, sizeof(BUSPACKAGE));  
  v->version = 1;
  return v;
}

void BUSPACKAGE_DESTROY(BUSPACKAGE *v)
{
  if(v)
  {
    free(v);
  }
}



 

