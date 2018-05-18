#include "lock.h"
#include "space.h"
#include "pool.h"

void new_pool(pool_t *pool, size_t size, int limit, int users_per_space) {
  pool = Malloc(sizeof(pool_t));
  pool->have = new_lock(limit);
  pool->head = NULL;
  pool->size = size;
  pool->limit = limit;
  pool->made = 0;
  pool->users_per_space = users_per_space;
}

space_t *get_space(pool_t *pool)
{
  space_t *space;
  get_lock(pool->have);

  // if a space is available, pull it from the free list and return it
  if (pool->head != NULL)
    {
      space = pool->head;
      get_lock(space->use);
      pool->head = space->next;
      space->len = 0;
      free_lock(pool->have);
      return space;
    }

  // create a new space
  assert(pool->limit != 0);
  if (pool->limit > 0)
    pool->limit--;
  pool->made++;
  space = new_space(space, pool->users_per_space, pool->size);
  space->pool = pool;
  free_lock(pool->have);
  return space;
}


void drop_space(space_t* space)
{
  if (space == NULL)
    return;
  pool_t pool = space->pool;
  get_lock(pool->have);
  free_lock(space->have);
  if (is_free(space->have))
    {
      space->next = pool->head;
      pool->head = space;
      space->len = 0;
    }
  free_lock(pool->have);
}

// Free the memory resources of a pool (unused buffers)
void free_pool(pool_t* pool)
{
  space_t *space;
  get_lock(pool->have);
  count = 0;
  if (pool->head == NULL)
    {
      free_lock(pool->have);
      return;
    }
    do
      {
	space = pool->head;
	pool->head = space->next;
	free(space->buf);
	free(space);
	pool->made--;
      } while (pool->head != NULL);
    free_lock(pool->have);
}
