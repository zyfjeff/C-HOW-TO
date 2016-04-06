#ifndef __MEMPOOL_H_
#define __MEMPOOL_H_

#define MEMORY_POOL_ALLOCED_CONST   ((void *) 0xFFFFFFFFu)
typedef struct memory_pool_s
{
  void *pool;           //内存池
  void *empty_blocks;   //空的blocks
  size_t block_size;    //block的大小
  size_t count;         //block的数量
} __attribute__ ((__aligned__)) memory_pool_t;

memory_pool_t * memory_pool_create(size_t bs, size_t c);
void memory_pool_destroy(memory_pool_t *mp);
void memory_pool_clear(memory_pool_t *mp);
void memory_pool_dump(memory_pool_t *mp, void (* print_func) (void *value));
void * memory_pool_alloc(memory_pool_t *mp);
void memory_pool_free(memory_pool_t *mp, void *p);

#endif /* __MEMPOOL_H_ */
