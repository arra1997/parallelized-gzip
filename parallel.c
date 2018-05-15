#include <pthread.h>
#include <semaphore.h>

typedef struct lock_t
{
  sem_t semaphore;
  unsigned int count;
} lock_t;

lock_t *new_lock(unsigned int users = 1)
{
  lock_t *lock;
  lock->count = users;
  lock->semaphore = malloc(sizeof(lock_t));
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
struct space
{
  lock_t *use;            // return to pool when unused
  unsigned char *buf;     // buffer of size size
  size_t size;            // current size of this buffer
  size_t len;             // for application usage (initially zero)
  struct pool *pool;      // pool to return to
  struct space *next;     // for pool linked list
};

static void new_space(struct space *space, unsigned int users, int size)
{
  space->use = new_lock();
  space->buf = calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
}

// Pool of spaces (one pool for each type needed).
struct pool
{
  lock_t *have;           // unused spaces available, for list
  struct space *head;     // linked list of available buffers
  size_t size;            // size of new buffers in this pool
  int limit;              // number of new spaces allowed, or -1
  int made;               // number of buffers made
  int users_per_space;
};

static void new_pool(struct pool *pool, size_t size, int limit, int users_per_space = 1) {
  pool->have = new_lock(limit);
  pool->head = NULL;
  pool->size = size;
  pool->limit = limit;
  pool->made = 0;
  pool->users_per_space = users_per_space;
}


static struct space *get_space(struct pool *pool)
{
  struct space *space;
  get_lock(pool->have);
  
  // if a space is available, pull it from the free list and return it
  if (pool->head != NULL)
    {
      space = pool->head;
      get_lock(space->use);
      pool->head = space->next;
      space->len = 0;
      return space;
    }

  // create a new space
  assert(pool->limit != 0);
  if (pool->limit > 0)
    pool->limit--;
  pool->made++;
  space = new_space(space, pool->users_per_space, pool->size);
  space->pool = pool;
}


