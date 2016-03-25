#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void *base = NULL;
#define align4(x) (((((x) - 1)>>2)<<2 )+4)

//内存块员数据就区域的表示
struct s_block {
    size_t          size;
    struct s_block  *next;
    struct s_block  *prev;
    int             free;
    void            *ptr;
    char            data[1];
};

typedef struct s_block *t_block;
#define BLOCK_SIZE  20
//将一个内存块，按照s大小分割成两半，在调用这个函数前，应该确定下分割后剩下的那个部分大小是否满足要求
//至少是BLOCK_SIZE+4
void split_block(t_block b,size_t s)
{
    t_block     new;
    //找到剩余内存块的元数据区域
    new      = (t_block)(b->data + s);
    //初始化元数据
    new->size = b->size - s - BLOCK_SIZE;
    new->next = b->next;
    new->prev = b;
    new->ptr  = new->data;
    //更新b内存块的元数据信息
    b->size   = s;
    b->next   = new;
    if(new->next)
        new->next->prev = new;

}

//当没有找到可用内存的时候调用这个函数分配一个新的内存块使用
t_block extend_heap(t_block last,size_t s)
{
    int     sb;
    t_block b;
    b   = sbrk(0);
    sb  = (int)sbrk(BLOCK_SIZE + s);
    if(sb < 0)
        return NULL;
    b->size = s;
    b->next = NULL;
    b->prev = last;
    b->ptr  = b->data;
    if(last)
        last->next = b;
    b->free = 0;
    return b;
}

t_block find_block(t_block *last,size_t size) {
    t_block b = base;
    while(b && !(b->free && b->size >= size)) {
        *last = b;
        b = b->next;
    }
    return (b);
}



void *t_malloc(size_t size) {
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



t_block fusion(t_block b) {
    if(b->next && b->next->free) {
        b->size += BLOCK_SIZE + b->next->size;
        b->next = b->next->next;
        if(b->next)
            b->next->prev = b;
    }
    return (b);
}

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



void t_free(void *p)
{
    t_block b;
    if(valid_addr(p))
    {
        b = get_block(p);
        b->free = 1;
        //判断前面的内存块是否t_free，如果t_free就合并
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


void copy_block(t_block src,t_block dst)
{
    int *sdata,*ddata;
    size_t  i;
    sdata = src->ptr;
    ddata = dst->ptr;
    for(i = 0;i*4 < src->size && i *4<dst->size;++i)
        ddata[i] = sdata[i];
}


void *t_realloc(void *p,size_t size)
{
    size_t  s;
    t_block b,new;
    void *newp;
    if(!p)  //为空就直接分配
        return (t_malloc(size));
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
                newp = t_malloc(s);
                if(!newp)
                    return (NULL);
                new = get_block(b);
                copy_block(b,new);
                t_free(p);
                return newp;
            }
        }
        return p;
    }
    return NULL;
}
