#include "lock.h"
#include <semaphore.h>
#include "utils.h"
#include <assert.h>

lock_t *new_lock(unsigned int users)
{
  lock_t *lock;
  lock = Malloc(sizeof(lock_t));
  lock->semaphore = Malloc(sizeof(sem_t));
  lock->count = users;
  assert(sem_init(lock->semaphore, 0, users) == 0);
  return lock;
}

void get_lock(lock_t* lock)
{
  assert(sem_wait(lock->semaphore) == 0);
}

int is_free(lock_t* lock)
{
  int value;
  sem_getvalue(lock->semaphore, &value);
  return (value == lock->count);
}

void free_lock(lock_t* lock)
{
  assert(sem_post(lock->semaphore) == 0);
}
