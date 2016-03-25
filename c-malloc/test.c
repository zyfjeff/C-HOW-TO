#include "alloc.h"

struct test{
    int data;
    int age;
};

int main()
{
    int *p = (int*)t_malloc(sizeof(4));
    *p = 5;
    t_free(p);
    struct test *z = (struct test *)t_malloc(sizeof(struct test));
    t_free(z);


}
