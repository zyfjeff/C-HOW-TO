#ifndef _LIB_T_ALLOC_H_
#define _LIB_T_ALLOC_H_
#include <sys/types.h>

extern void * t_malloc(void *p);
extern void *t_realloc(void *p,size_t size);
extern void t_free(void *p);
extern void *calloc(size_t number,size_t size);


#endif //end of _LIB_T_ALLOC_H_
