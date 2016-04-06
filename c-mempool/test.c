#include <stdio.h>
#include <stdlib.h>
#include "mempool.h"

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

