#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include "parallel.h"
#include "utils.h"

#define DICT 32768U
#define INBUFS(p) (((p)<<1)+3)
#define OUTPOOL(s) ((s)+((s)>>4)+DICT)
#define RSYNCBITS 12

struct lock_t
{
  sem_t *semaphore;
  unsigned int count;
};

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

// -- pool of spaces for buffer management --

// These routines manage a pool of spaces. Each pool specifies a fixed size
// buffer to be contained in each space. Each space has a use count, which when
// decremented to zero returns the space to the pool. If a space is requested
// from the pool and the pool is empty, a space is immediately created unless a
// specified limit on the number of spaces has been reached. Only if the limit
// is reached will it wait for a space to be returned to the pool. Each space
// knows what pool it belongs to, so that it can be returned.

// A space (one buffer for each space).
struct space_t
{
  lock_t *use;            // return to pool when unused
  unsigned char *buf;     // buffer of size size
  size_t size;            // current size of this buffer
  size_t len;             // for application usage (initially zero)
  pool_t *pool;      // pool to return to
  space_t *next;     // for pool linked list
};

space_t *new_space(unsigned int users, int size)
{
  space_t *space;
  space = Malloc(sizeof(space_t));
  space->use = new_lock(1);
  space->buf = Calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
}

// Pool of spaces (one pool for each type needed).
struct pool_t
{
  lock_t *have;           // unused spaces available, for list
  space_t *head;     // linked list of available buffers
  size_t size;            // size of new buffers in this pool
  int limit;              // number of new spaces allowed, or -1
  int made;               // number of buffers made
  int users_per_space;
};

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
  space = new_space(pool->users_per_space, pool->size);
  space->pool = pool;
  free_lock(pool->have);
  return space;
}


void drop_space(space_t* space)
{
  if (space == NULL)
    return;
  pool_t *pool = space->pool;
  get_lock(pool->have);
  free_lock(space->use);
  if (is_free(space->use))
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
  int count;
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
struct job_t
{
  long seq;                   // sequence number
  int more;                   // true if this is not the last chunk
  space_t *in;                // input data to compress
  space_t *out;               // dictionary or resulting compressed data
  space_t *lens;              // coded list of flush block lengths
  unsigned long check;        // check value for input data
  lock *calc;                 // released when check calculation complete
  job_t *next;           // next job in the list (either list)
};

void new_job (job_t *job, long seq, space_t *in, space_t *out, space_t *lens)
{
  job->seq = seq;
  job->in = in;
  job->out = out;
  job->lens = lens;
  job->check = 0;
  job->calc = new_lock(1);
  job->next = NULL;
}

void free_job (job_t *job)
{
  free_lock (job->calc);
}

struct job_queue_t
{
  job_t *head;     // linked list of jobs
  job_t *tail;
  int len;         // length of job linked list
  lock_t *use;
};

void new_job_queue (job_queue_t *job_q)
{
  job_q->head = NULL;
  job_q->tail = NULL;
  job_q->len = 0;
  job_q->use = new_lock(1);
}

void free_job_queue (job_queue_t *job_q)// not thread safe
{
  while (job_q->head != NULL)
  {
    job_t *temp = job_q->head;
    job_q->head = job_q->head->next;
    free_job (temp);
  }
  free_lock(job_q->use);
}


//get a job from the beginning of the job queue
//the job should be freed if not put back to job queue after usage
job_t *get_job_bgn (job_queue_t *job_q)
{
  if (job_q->head == NULL)
    return NULL;
  job_t *ret = job_q->head;
  job_q->head = job_q->head->next;
  if (job_q->head == NULL)
    job_q->tail = NULL;
  ret->next = NULL;
  return ret;
}

//add a job to the beginning of the job queue
void add_job_bgn (job_queue_t *job_q, job_t *job)
{
  if (job_q->head == NULL)
    job_q->tail = job;
  job->next = job_q->head;
  job_q->head = job;
  return;
}

//add a job to the end of the job queue
void add_job_end (job_queue_t *job_q, job_t *job)
{
  if (job_q->tail == NULL)
  {
    job_q->head = job;
    job_q->tail = job;
    job->next = NULL;
    return;
  }
  job_q->tail->next = job;
  job_q->tail = job;
  job->next = NULL;
  return;
}

// Command the compress threads to all return, then join them all (call from
// main thread), free all the thread-related resources.
local void finish_jobs(queue_t job_queue) {
    struct job_t job;
    int caught;

    /*
    // command all of the extant compress threads to return
    possess(compress_have);
    job.seq = -1;
    job.next = NULL;
    compress_head = &job;
    compress_tail = &(job.next);
    twist(compress_have, BY, +1);       // will wake them all up
    */
    job.seq = -1;
    job.next = NULL;
    job_queue.add_job_bgn(job);

    // join all of the compress threads, verify they all came back
    caught = join_all();
    //Trace(("-- joined %d compress threads", caught));
    assert(caught == cthreads);
    cthreads = 0;

    // free the resources
    caught = free_pool(&lens_pool);
    //Trace(("-- freed %d block lengths buffers", caught));
    caught = free_pool(&dict_pool);
    //Trace(("-- freed %d dictionary buffers", caught));
    caught = free_pool(&out_pool);
    //Trace(("-- freed %d output buffers", caught));
    caught = free_pool(&in_pool);
    //Trace(("-- freed %d input buffers", caught));
    //free_lock(write_first);
    //free_lock(compress_have);
    //compress_have = NULL;

    free_job_queue(job_queue);
}
