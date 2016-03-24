#include <stdio.h>
#include <setjmp.h>

//C不支持在switch中使用goto，为此这里为了实现FINALLY语义，将所有的case语句放在while(1)
//循环中，当遇到某个case，执行完case内的内容后，使用break跳出的是while循环，然后，借助
//C语言中的switch case的full throught，直接跳转到default，从而实现了FINALLY语义
#define TRY do{jmp_buf ex_buf__;switch(setjmp(ex_buf__)) {case 0:while(1) {
#define CATCH(x)break;case x:
#define FINALLY break; } default: {
#define ETRY break;} } }while(0)
#define THROW(x) longjmp(ex_buf__,x)


#define FOO_EXCEPTION (1)
#define BAR_EXCEPTION (2)
#define BAZ_EXCEPTION (3)

int main(int argc,char **argv)
{
    TRY
    {
        printf("In try Statement\n");
        THROW(BAR_EXCEPTION);
        printf("I do not apper\n");
    }
    CATCH(FOO_EXCEPTION)
    {
        printf("Got Foo!\n");
    }
    CATCH(BAR_EXCEPTION)
    {
        printf("Got Bar!\n");
    }
    CATCH(BAZ_EXCEPTION)
    {
        printf("Got Baz!\n");
    }
    FINALLY
    {
        printf("...et in arcadia Ego\n");
    }
    ETRY;
    return 0;
}
