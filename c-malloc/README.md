# 堆内存
堆内存是连续的，但是有三个界限，一个是起始点，另一个则是映射区域的终点称为break点，最后一个则是整个堆区域的最大点。
通过在头文件sys/ressource.h中的getrlimit方法和setrlimit方法，可以用来或许堆的最大点。通过brk可以得到break点

## brk and sbrk
brk 可以设置break为指定地址,如果该区域没有映射将会自动映射
sbrk则是移动break点，不能直接指定地址移动到指定位置，而是提供移动距离。当sbrk指定的参数是0的时候，返回当前的break地址
内存映射总是按照页来映射的，但是break点可以不在页边界上，这就导致了从break点到页边界这段内存是可用的，但是建议使用

## mmap
mmap用于直接将文件映射到内存中，但是mmap有一种匿名映射，可以用来实现malloc,此时分配的内存不属于堆内存，OpenBsd的malloc
实现就是利用了mmap来做的。

## dummy malloc
直接使用sbrk来移动break点，这就是最简单的一种,malloc的实现,很显然，这个malloc是dummy的，无法使用的，因为当多次malloc
后，已经无法去free之前分配的内存了，因为没有记录每次分配的时候的大小,不知道要回收多少内存

## 如何去实现
为了记录每次分配的大小，需要给每次分配的内存添加一些元数据信息，比如该块内存是否分配，分配了多大，为了方便内存的合并还需要
知道下一段分配的内存位置，因此需要有个指针指向下段内存的起始位置，这样方便在回收的时候可以对一系列的段进行合并。所有每段分配
的内存都需要有一个元数据区域，下面是这个元数据区域的C表示:

```
typedef struct s_block *t_block;
struct s_block {
    size_t  size;
    t_block next;
    int     free;
};
```

##  地址对齐
为了让分配的内存对齐，需要调整分配的大小是系统位数的整数倍 32位系统中，32bits是4字节，因此分配的内存应该是4的整数倍。
假设分配的大小为x那么算法如下:

```
x = 4*p + q ( 0 <= q <=3),如果x是4的整数倍那么q就是0
x - 1 = 4*(p-1) + 3,因此(x-1)/4*4+4 = 4*p = x 如果q!=0的化，
x - 1 = 4*p + q-1(0<= q-1 <=2)因此 (x-1)/4*4+4 = 4*p+4 = x/4*4 + 4

最终(x-1)/4*4+4 总是最接近4的整数倍，用C语言实现如下:
#define align4(x) (((((x) - 1)>>2)<<2 )+4)
```

## First fit算法
当malloc运行一段时间后，会维护了一段段的内存块，散落在堆区域内，无法进行合并，当下次再分配的时候，如何从这些零碎的区域中
选择一个进行分配呢，这里使用了first fit算法，算法代码如下:

```
t_block find_block(t_block *last,size_t size) {
    t_block b = base;
    while(b && !(b->free && b->size >= size)) {
        *last = b;
        b = b->next;
    }
    return (b);
}
```
遍历所有分配的内存块，找到第一个大小大于请求的大小,然后返回这块内存的起始地址即可

## 扩展堆
当没有可用的内存块的时候，该如何分配内存呢，只需要移动break就可以完成。代码如下:

```
#define BLOCK_SIZE sizeof(struct s_block)
t_block extend_heap(t_block last,size_t s) {
    t_block b;
    b = sbrk(0);
    if(sbrk(BLOCK_SIZE + s) == (void*)-1)
        return (NULL);
    b->size = s;
    b->next = NULL;
    if(last)
        last->next = b;
    b->free = 0;
    return (b);
}
```
使用sbrk移动break指定大小，然后初始化一个元数据结构体，然后再使用last指针把新分配的区域链接起来

## 分割内存块
但请求的内存大小通过first fit算法找到了合适的块后，因为请求的大小和找到的块大小不是完全一样的，因此需要切割这个块
切割的时候需要考虑一些问题，比如切割后应该保证剩下的内容大小至少是BLOCK_SIZE+4.为了方便计算剩余空间的其实位置，可以使用
一个小技巧，就是给元数据区域增加一个长度为0的的数组，(可变数组，C中因为不能使用0长度，因此这里使用1)，这个数组的地址
就是数据区域的起始地址。有个这个起始地址就方便计算下段区域的地址了，因此修改元数据的结构体如下:

```
struct s_block {
    size_t  size;
    t_block next;
    int     free;
    char    data[1];
};
```
上面的这个data是不占用空间的.

```
void split_block(t_block b,size_t s) {
    t_block new;
    new = b->data + s; //数据区域的其实地址加上这段内存块的数据大小，就是剩余空间的起始地址了
    new->size = b->size -s -BLOCK_SIZE;
    new->next = b->next;
    new->free = 1;
    b->size = s;
    b->next = new;
}
```
使用new指向剩余空间的起始位置，然后初始化new，并链接，在调用这个函数的时候，应该事先判断下剩余的空间是否满足最小大小

## malloc实现
malloc主要是组合上面提到的一些函数，初始化base，也就是第一块被分配的内存块。是个全局的变量，在first fit算法中用到了，因为
需要从第一个被分配的内存块开始搜索未分配的内存块。malloc算法实现

```
void *base = NULL;
void *malloc(size_t size) {
    t_block b,last;
    size_t  s;
    //字节对齐
    s = align4(size);
    //是否是第一次分配
    if(base) {
        last = base;
        //first fit算法查找可用内存块
        b = find_block(&last,s);
        //是否查找到,查找到后进行分割，否则扩展现有的内存
        if(b) {
            if((b->size -s ) >= (BLOCK_SIZE + 4))   
                split_block(b,s);
            b->free = 0;
        } else {
            b = extend_heap(last,s);
            if(!b)
                return (NULL);
        }
    } else {
        b = extend_heap(NULL,s);
        if(!b)
            return (NULL);
        base = b;
    }
    return (b->data);
}
```


## calloc实现
calloc的实现比较简单，先使用malloc来得到正确的大小后，然后初始化这段内存为0即可，实现如下:

```
void *calloc(size_t number,size_t size) {
    size_t *new;
    size_t s4,i;
    new = malloc(number*size);
    if(new) {
        //因为是4字节对齐，因此，每次初始化4字节，如果分配60字节只需要初始化15次
        //因此右移2位，相当于除以4。
        s4 = align4(number * size) >> 2;
        for(i = 0;i < s4;++i) {
            new[i] = 0;    
        }
    }
    return (new);
}
```
## free的实现
free如果不考虑合并的话，还是非常简单的，得到内存块的元数据指针，设置free标记，就完成了。但是这样会导致大量的内存碎片
为此在free的时候需要考虑合并相邻的内存块，但是如何和内存块的前后进行合并呢，因为使用的是单链表链接所有的内存块的，无法
o(1)找到前一块内存，为此必须从头寻找到前一块内存块，然后合并。为此可以考虑使用双向循环链表。因此元数据区域更改为如下:

```
struct s_block {
    size_t  size;
    struct s_block  *next;
    struct s_block  *prev;
    int             free;
    char            data[1];
};
typedef struct s_block *t_block;
```

有了双向循环链表作为支撑，这下内存块的合并就方便了，其代码如下:

```
t_block fusion(t_block b) {
    if(b->next && b->next->free) {
        b->size += BLOCK_SIZE + b->next->size;
        b->next = b->next->next;
        if(b->next)
            b->next->prev = b;
    }
    return (b);
}
```

到此为此内存碎片的这个问题，有所优化了，但是还存在另外一个问题就是，如何判断用户传入的指针是指向数据区域的开始位置，如果不是开始
位置那么获取到的元数据就是错误的，会导致错误的free，(因为元数据的地址是根据数据起始地址减去元数据大小得到的)，为此
需要有个手段来判断传入的指针是否是有效的，因此需要在元数据区域添加一个指针指向数据结构的起始地址，然后拿这个地址和用户传入
的地址进行比较就可以判断是否有效，因此元数据被修改为如下形式:

```
struct s_block {
    size_t  size;
    struct s_block  *next;
    struct s_block  *prev;
    void            *ptr;
    int             free;
    char            data[1];
};
```

下面是判断传入的指针是否有效的代码实现:

```
//返回元数据指针
t_block get_block(void *p)
{
    char *tmp;
    tmp = p;
    return (p = tmp -=BLOCK_SIZE);
}

int valid_addr(void *p)
{
    if(base)
    {
        if(p > base && p<sbrk(0))
        {
            return (p == (get_block(p))->ptr);   
        }
    }
    return 0;
}
```

下面是free函数的代码实现:

```
void free(void *p)
{
    t_block b;
    if(valid_addr(p))
    {
        b = get_block(p);
        b->free = 1;
        //判断前面的内存块是否free，如果free就合并
        if(b->prev && b->prev->free)
            b = fusion(b->prev); 
            //和后面的内存合并
            if(b->next)
                fusion(b);
            else { //b是最后一段内存快
                if(b->prev)  
                    b->prev->next = NULL;
                else //如果只有b，那么就直接brk回收掉b,然后设置base = NULL
                    base = NULL;
                brk(b);
            }
    }
}
```

## realloc的实现
realloc的实现主要分为下面几步:

* 使用malloc分配指定大小的一个新的内存块
* 从原有区域拷贝数据到新的内存块
* free掉原有的内存块
* 返回新的指针

但是在实现过程中还是需要考虑一些细节上的问题，比如:
* 如果realloc的大小没有改变，那么什么也不做
* 如果realloc是减小内存块大小，那么只需要分割就可以了
* 如果下一个block是可用的，并且提供了足够的空间，那么只要合并，然后分割即可
 
其实现代码如下:

```

void copy_block(t_block src,t_block dst)
{
    int *sdata,*ddata;
    size_t  i;
    sdata = src->ptr;
    ddata = dst->ptr;
    for(i = 0;i*4 < src->size && i *4<dst->size;++i)
        ddata[i] = sdata[i];
}

void *realloc(void *p,size_t size)
{
    size_t  s;
    t_block b,new;
    void *newp;
    if(!p)  //为空就直接分配
        return (malloc(size));
    if(valid_addr(p)) //是否是合法的地址
    {
        s = align4(size);
        b = get_block(p);
        if(b->size >= s) { //realloc的大小是小于目前的内存块大小，隐私split即可
            if(b->size -s >= (BLOCK_SIZE + 4)) 
                split_block(b,s);
        } else {
            //进行合并策略,判断是否可以合并，和合并后的大小是否符合要求
            if(b->next && b->next->free
               && (b->size + BLOCK_SIZE + b->next->size) >=s) {
                fusion(b);
                //可以合并，合并后，进行split即可
                if(b->size -s >= (BLOCK_SIZE + 4))
                    split_block(b,s);
                    //都不满足的情况直接分配一块新的内存块，然后拷贝数据
            } else {
                newp = malloc(s);
                if(!newp)
                    return (NULL);
                new = get_block(b,new);
                copy_block(b,new);
                free(p);
                return newp;
            }
        }
        return p;
    }
    return NULL;
}
```

## 总结

