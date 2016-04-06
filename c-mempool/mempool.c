#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mempool.h"

/*
 *  设置内存池对象信息，比如分配内存池，设置内存池的block大小和数量
 *  然后分配一段block大小*数量的一段内存，然后使用链表将这些block串联起来
 */
memory_pool_t * memory_pool_create(size_t bs, size_t c)
{
  memory_pool_t *mp = malloc(sizeof(memory_pool_t));
  if (!mp)
    return NULL;

  mp->block_size = bs;
  mp->count = c;
  //分配了mp->count个block，每个block多出一个指针大小，用于指向下一个block
  mp->pool = malloc((mp->block_size + sizeof(void *)) * mp->count);
  //初始化每个block中多出来的那个指针，指向下一个block
  memory_pool_clear(mp);
  mp->empty_blocks = mp->pool;

  return mp;
}

void memory_pool_destroy(memory_pool_t *mp)
{
  if (!mp)
    return;

  memory_pool_clear(mp);
  free(mp->pool);
  free(mp);
}


/*
 *
 *  将所有的block通过内部指针连接起来
 *
 */
void memory_pool_clear(memory_pool_t *mp)
{
  if (!mp)
    return;

  size_t i;
  void **p;

  for (i = 0; i < mp->count - 1; i++)
  {
    p = (void **) ((uint8_t *) mp->pool + (mp->block_size * (i + 1) +
                   sizeof(void *) * i));
    *p = (uint8_t *) mp->pool + (mp->block_size + sizeof(void *)) * (i + 1);
  }
  //最后一个block的指针指向NULL
  p = (void **) ((uint8_t *) mp->pool + (mp->block_size * mp->count +
                 sizeof(void *) * (mp->count - 1)));
  *p = NULL;
  //empty_blocks始终指向第一个空闲的block
  mp->empty_blocks = mp->pool;
}

void memory_pool_dump(memory_pool_t *mp, void (* print_func) (void *value))
{
  printf("start: %p, size: %d, count: %d\n", mp->pool,
         (mp->block_size + sizeof(void *)) * mp->count, mp->count);

  void *block;
  void **next;
  size_t i;

  for (i = 0; i < mp->count; i++)
  {
    block = (void *) ((uint8_t *) mp->pool + (mp->block_size * i) +
                      sizeof(void *) * i);
    next = (void **) ((uint8_t *) block + mp->block_size);

    printf("block #%i(%p):", i, block);

    if (*next == MEMORY_POOL_ALLOCED_CONST)
    {
      printf(" allocated");

      if (print_func)
      {
        printf(", value: ");
        print_func(block);
      }

      printf("\n");
    } else
    {
      printf(" free, next address %p\n", *next);
    }
  }
}

/*
 *  从内存池对象中分配一个block
 *
 */
void * memory_pool_alloc(memory_pool_t *mp)
{
  void *p;
// 查看是否有空闲的blocks
  if (mp->empty_blocks)
  {
    p = mp->empty_blocks;
//指向下一个block,设置内部指针指向MEMORY_POOL_ALLOCED_CONST
    mp->empty_blocks = * (void **) ((uint8_t *) mp->empty_blocks +
                                    mp->block_size);
    *(void **) ((uint8_t *) p + mp->block_size) = MEMORY_POOL_ALLOCED_CONST;
    return p;
  } else
  {
    return NULL;
  }
}

/*
 *  回收block
 *
 */
void memory_pool_free(memory_pool_t *mp, void *p)
{
//判断p的有效性，是否为NULL，是否在内存池范围内
  if (p && (p >= mp->pool) && (p <= (void *) ((uint8_t *) mp->pool +
      (mp->block_size + sizeof(void *)) * mp->count)))
  {
    //重置p的内部指针指向第一个空的block，然后重新设置第一个空的block位置
    *(void **) ((uint8_t *) p + mp->block_size) = mp->empty_blocks;
    mp->empty_blocks = p;
  }
}

