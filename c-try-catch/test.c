#include <stdio.h>
#include "try-catch.h"

#define FOO_EXCEPTION (1)
#define BAR_EXCEPTION (2)
#define BAZ_EXCEPTION (3)

int main()
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
