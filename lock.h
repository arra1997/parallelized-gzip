#include <semaphore.h>

typedef struct 
{
  sem_t *semaphore;
  unsigned int count;
}lock_t;


lock_t *new_lock(unsigned int users);

void get_lock(lock_t* lock);

int is_free(lock_t* lock);

void free_lock(lock_t* lock);
