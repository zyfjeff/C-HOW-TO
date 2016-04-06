# mempool implement
==
## Introduction
什么是内存池?我认为我必须首先回答这个问题，因为这对一个新手来说是很重要的，简而言之，内存池就是一个内存块，避免了每次去
调用malloc/free或者new/delete来分配内存，这个技术的优点就在于重用了现有的内存块，减少了调用系统调用的次数。

## Background
为什么要使用内存池?有两个原因

* 减少系统分配内存的次数
* 避免了内存碎片

注: 内存池仅仅是在程序频繁使用malloc/free或new/delete的时候才是有价值的，除此之外你不应该使用内存池。

## Implementation
首先来看下，这个内存池提供的接口。

```
#ifndef __MEMPOOL_H_
#define __MEMPOOL_H_
//如果一个已经分配的内存块，其内部指针就会指向这个常量，用于表面该BLOCK是已经分配的
#define MEMORY_POOL_ALLOCED_CONST   ((void *) 0xFFFFFFFFu)
//内存池对象，维护该内存池对象的信息
typedef struct memory_pool_s
{
    void *pool;           //内存池，实际分配内存块的地方
    void *empty_blocks;   //保存第一个空的内存块的位置  
    size_t block_size;    //内存块的大小
    size_t count;         //内存块的数量
} __attribute__ ((__aligned__)) memory_pool_t; //结构体对齐

memory_pool_t * memory_pool_create(size_t bs, size_t c);
void memory_pool_destroy(memory_pool_t *mp);
void memory_pool_clear(memory_pool_t *mp);
void memory_pool_dump(memory_pool_t *mp, void (* print_func) (void *value));
void * memory_pool_alloc(memory_pool_t *mp);
void memory_pool_free(memory_pool_t *mp, void *p);

#endif /* __MEMPOOL_H_ */

上面的接口描述如下:
memory_pool_create　用于创建一个内存池对象，分配内存和初始化内存池对象的一些数据成员
memory_pool_destroy 释放内存池对象所维护的内存块，以及内存池对象本身
memory_pool_clear 使用单链表将内存池对象内的所有内存块连接起来
memory_pool_dump 打印内存池
memory_pool_alloc 分配一个内存块
memory_pool_free 释放一个内存块

```
上面的接口还是很容易使用的，下面是一段示例程序，演示了如何去使用上面的这个内存池。

```
void print_intptr(void *p)
{
  printf("%d", *(int *) p);
}

int main(int argc, char *argv[])
{
  int *value;
  int c = 0;
  memory_pool_t *mp = memory_pool_create(sizeof(int), 128);
  memory_pool_dump(mp, print_intptr);
  while ((value = memory_pool_alloc(mp)))
  {
    *value = c++;
  }
  memory_pool_dump(mp, print_intptr);
  memory_pool_destroy(mp);
  return EXIT_SUCCESS;
}

```
上面这段程序，创建了一个128个4字节内存块的内存池，然后在这个内存池中分配出去128个内存块。在
分配出去之前调用了memory_pool_dump打印了内存池的内存块信息，然后又在分配后打印了一次。
我相信通过上面的介绍，现在我们已经对内存池有了一个初步的映像，下面我们将一步一步去实现这个内存池。

### memory_pool_create实现

这个接口用于创建内存池对象，然后初始化内存池对象中的一些数据结构

```
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

```
很显然上面这段代码主要是为了初始化memory_pool_t，最核心的部分就是memory_pool_clear的实现，这个接口用于构建内存块单链表
下面我们来看看这个接口的内部实现吧。

### memory_pool_t的实现

在分配内存块的时候，每个内存块多出了一个指针大小,多出的这个指针就是用来构建单链表的，下面这个接口的目的就在于此。

```
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
    //得到内部指针，强转为双指针，用于保存下一个内存块的地址
    p = (void **) ((uint8_t *) mp->pool + (mp->block_size * (i + 1) +
                   sizeof(void *) * i));
    //保存下一个内存块的地址
    *p = (uint8_t *) mp->pool + (mp->block_size + sizeof(void *)) * (i + 1);
  }
  //最后一个block的指针指向NULL
  p = (void **) ((uint8_t *) mp->pool + (mp->block_size * mp->count +
                 sizeof(void *) * (mp->count - 1)));
  *p = NULL;
  //empty_blocks始终指向第一个空闲的block
  mp->empty_blocks = mp->pool;
}
```
上面的构建过程如下图所示

内存池对象已经创建完毕，内存块也已经通过单链表链接起来，通过empty_blocks可以高效率的得到第一个可用的内存块。那么
接下来让我们看看如何分配内存块吧。

### memory_pool_alloc实现
从内存池分配一个内存块还是很简单的，只需要从内存块的链表中摘下一个空闲的内存块，然后重新设置empty_bolcks指针即可。
下面是代码实现。

```
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
//指向下一个block,并将分配的内存块的内部指针指向MEMORY_POOL_ALLOCED_CONST,表明是已经分配的内存块
    mp->empty_blocks = * (void **) ((uint8_t *) mp->empty_blocks +
                                    mp->block_size);
    *(void **) ((uint8_t *) p + mp->block_size) = MEMORY_POOL_ALLOCED_CONST;
    return p;
  } else
  {
    return NULL;
  }
}
```

### memory_pool_free实现

将回收的内存块按照头插法的方式插入到单链表中，然后重新设置empty_blocks指针即可，其代码如下:

```
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
    //重置p的内部指针指向第一个空的block，然后重新设置empty_blocks指针
    *(void **) ((uint8_t *) p + mp->block_size) = mp->empty_blocks;
    mp->empty_blocks = p;
  }
}

```

### memory_pool_destroy

释放内存池内部维护的所有内存块，最后释放内存池对象本身，其代码实现如下:

```
void memory_pool_destroy(memory_pool_t *mp)
{
  if (!mp)
    return;
  memory_pool_clear(mp);
  free(mp->pool);
  free(mp);
}

```

代码很简单，核心代码就是两个free，一个释放内存块，一个是释放内存池对象本身，在释放之前调用了memory_pool_clear，
其目的是为了重新设置内存块的内部指针，让内存块的状态变成了未分配状态。

### memory_pool_dump实现
最后来看一看memory_pool_dump的实现，这个接口是用于内存池调试而用，主要就是打印内存块中的内容，和内部指针保存的地址信息
其代码实现如下:

```
void memory_pool_dump(memory_pool_t *mp, void (* print_func) (void *value))
{
    //打印一下内存池对象信息，例如内存块起始地址，内存块数量，内存块总大小等
  printf("start: %p, size: %d, count: %d\n", mp->pool,
         (mp->block_size + sizeof(void *)) * mp->count, mp->count);

  void *block;
  void **next;
  size_t i;
    //遍历所有的内存块
  for (i = 0; i < mp->count; i++)
  {
    block = (void *) ((uint8_t *) mp->pool + (mp->block_size * i) +
                      sizeof(void *) * i);
    next = (void **) ((uint8_t *) block + mp->block_size);
    //打印内存块的编号和地址
    printf("block #%i(%p):", i, block);
    //判断内存块是否分配，如果分配了，就调用用户传入的print_func函数，来打印内存块中的内容
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
    //如果没有分配就打印内部指针保存的地址
      printf(" free, next address %p\n", *next);
    }
a  }
}

```
思路很简单，就是遍历所有的内存块，然后打印内存块的信息，因为内存块中保存的可能是一个自定义数据类型，因此需要用户
自己定义打印输出的逻辑。所以memory_pool_dump提供了一个参数用于接收用户的打印函数。

## 总结
通过上面的分析，本文实现了一个简易的内存池，但是仍然有很多的不足之处，比如，这个内存池是非线程安全的，这个内存池目前
只能分配固定大小的内存块。真正可用的内存池应该使用slab机制，预先创建多个不同大小的内存块供分配。










