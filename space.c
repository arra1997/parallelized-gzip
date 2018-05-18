#include "lock.h"
#include "utils.h"
#include "space.h"

void new_space(space_t *space, unsigned int users, int size)
{
  space = Malloc(sizeof(space_t));
  space->use = new_lock(1);
  space->buf = Calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
}


