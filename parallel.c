#include <pthread.h>
#include <semaphore.h>
#include "utils.h"

typedef struct lock_t
{
  sem_t semaphore;
  unsigned int count;
} lock_t;

lock_t *new_lock(unsigned int users = 1)
{
  lock_t *lock;
  lock = Malloc(sizeof(lock_t));
  lock->count = users;
  lock->semaphore = Malloc(sizeof(lock_t));
  assert(sem_init(&(lock.semaphore), 0, users) == 0);
  return lock;
}

void get_lock(lock_t* lock)
{
  assert(sem_wait(lock->semaphore) == 0);
}

int is_free(lock_t* lock)
{
  int value;
  sem_getvalue(lock->semaphore, value);
  return (value == count);
}

void free_lock(lock_t* lock)
{
  assert(sem_post(lock->semaphore) == 0);
}

// -- pool of spaces for buffer management --

// These routines manage a pool of spaces. Each pool specifies a fixed size
// buffer to be contained in each space. Each space has a use count, which when
// decremented to zero returns the space to the pool. If a space is requested
// from the pool and the pool is empty, a space is immediately created unless a
// specified limit on the number of spaces has been reached. Only if the limit
// is reached will it wait for a space to be returned to the pool. Each space
// knows what pool it belongs to, so that it can be returned.

// A space (one buffer for each space).
typedef struct space_t
{
  lock_t *use;            // return to pool when unused
  unsigned char *buf;     // buffer of size size
  size_t size;            // current size of this buffer
  size_t len;             // for application usage (initially zero)
  pool_t *pool;      // pool to return to
  space_t *next;     // for pool linked list
} space_t;

static void new_space(space_t *space, unsigned int users, int size)
{
  space = Malloc(sizeof(space_t));
  space->use = new_lock();
  space->buf = Calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
}

// Pool of spaces (one pool for each type needed).
typedef struct pool_t
{
  lock_t *have;           // unused spaces available, for list
  space_t *head;     // linked list of available buffers
  size_t size;            // size of new buffers in this pool
  int limit;              // number of new spaces allowed, or -1
  int made;               // number of buffers made
  int users_per_space;
} pool_t;

static void new_pool(pool_t *pool, size_t size, int limit, int users_per_space = 1) {
  pool = Malloc(sizeof(pool_t));
  pool->have = new_lock(limit);
  pool->head = NULL;
  pool->size = size;
  pool->limit = limit;
  pool->made = 0;
  pool->users_per_space = users_per_space;
}


static space_t *get_space(pool_t *pool)
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


static void drop_space(space_t* space)
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
  
// -- job queue used for parallel compression --

// Compress or write job (passed from compress list to write list). If seq is
// equal to -1, compress_thread is instructed to return; if more is false then
// this is the last chunk, which after writing tells write_thread to return.
struct job {
  long seq;                   // sequence number
  int more;                   // true if this is not the last chunk
  space_t *in;                // input data to compress
  space_t *out;               // dictionary or resulting compressed data
  space_t *lens;              // coded list of flush block lengths
  unsigned long check;        // check value for input data
  lock *calc;                 // released when check calculation complete
  struct job *next;           // next job in the list (either list)
};



