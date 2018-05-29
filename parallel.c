#include <config.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <zlib.h>
#include "parallel.h"
#include "utils.h"
#include <stdint.h>
#include <string.h>


// Sliding dictionary size for deflate.
#define DICT 32768U
// Largest power of 2 that fits in an unsigned int. Used to limit requests to
// zlib functions that use unsigned int lengths.
// #define MAXP2 (UINT_MAX - (UINT_MAX >> 1))

//TODO MOVE THIS GLOBAL STRUCT TO THE MAIN FILE
struct {
    int cthreads;
    int procs;
    int block;
} g;

struct lock_t
{
  sem_t *semaphore;
  int fixed_size;
};


lock_t *new_lock(unsigned int users, int fixed_size)
{
  lock_t *lock;
  lock = Malloc(sizeof(lock_t));
  lock->semaphore = Malloc(sizeof(sem_t));
  lock->fixed_size = fixed_size;
  assert(sem_init(lock->semaphore, 0, users) == 0);
  return lock;
}


// Destroy the lock.
// This is not safe deletion. Only use this
// when you know nobody needs the lock anymore.
void free_lock(lock_t* lock)
{
  free(lock->semaphore);
  free(lock);
}

void get_lock(lock_t* lock)
{
  assert(sem_wait(lock->semaphore) == 0);
}

void release_lock(lock_t* lock)
{
  assert(sem_post(lock->semaphore) == 0);
}

void increment_lock(lock_t* lock)
{
  if (lock->fixed_size)
    return;
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
  unsigned char *buf;     // buffer of size size
  size_t size;            // current size of this buffer
  size_t len;             // for application usage (initially zero)
  pool_t *pool;      // pool to return to
  space_t *next;     // for pool linked list
};


space_t *new_space(int size)
{
  space_t *space;
  space = Malloc(sizeof(space_t));
  space->buf = Calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
  return space;
}


// Destroy the space.
// This is not safe deletion. Only call this when you know
// nobody needs this space anymore.
void free_space(space_t *space)
{
  free(space->buf);
  free(space);
}


// Pool of spaces (one pool for each type needed).
struct pool_t
{
  lock_t *have;           // unused spaces available, for list
  lock_t *safe;           //ensures safe use of pool in multithreaded environment
  space_t *head;     // linked list of available buffers
  size_t size;            // size of new buffers in this pool
  int limit;              // number of new spaces allowed
  int made;               // number of buffers made
};

pool_t *new_pool(size_t size, int limit) {
  pool_t *pool;
  pool = Malloc(sizeof(pool_t));
  pool->have = new_lock(limit, 1);
  pool->safe = new_lock(1, 1);
  pool->head = NULL;
  pool->size = size;
  pool->limit = limit;
  pool->made = 0;
  return pool;
}


space_t *get_space(pool_t *pool)
{
  space_t *space;
  get_lock(pool->have);
  get_lock(pool->safe);

  // if a space is available, pull it from the free list and return it
  if (pool->head != NULL)
    {
      space = pool->head;
      pool->head = space->next;
      space->len = 0;
      release_lock(pool->safe);
      return space;
    }

  // create a new space
  assert(pool->made != pool->limit);
  pool->made++;
  space = new_space(pool->size);
  space->pool = pool;
  release_lock(pool->safe);
  return space;
}


void drop_space(space_t* space)
{
  if (space == NULL)
    return;
  pool_t *pool = space->pool;
  get_lock(pool->safe);
  space->next = pool->head;
  pool->head = space;
  space->len = 0;
  release_lock(pool->have);
  release_lock(pool->safe);
}

// Destroy the pool.
// This is not safe deletion. Only call this when you
// know nobody needs the pool anymore.
void free_pool(pool_t* pool)
{
  space_t *space;
  get_lock(pool->safe);
  do
    {
      space = pool->head;
      pool->head = space->next;
      free_space(space);
    } while (pool->head != NULL);
  free_lock(pool->safe);
  free_lock(pool->have);
  free(pool);
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
  space_t *dict;
  unsigned long check;        // check value for input data
  lock_t *calc;                 // released when check calculation complete
  job_t *next;           // next job in the list (either list)
};


job_t *new_job (long seq, pool_t *in_pool, pool_t *out_pool, pool_t *lens_pool)
{
  job_t *job = Malloc(sizeof(job_t));
  job->seq = seq;
  job->in = get_space(in_pool);
  job->out = get_space(out_pool);
  job->lens = get_space(lens_pool);
  job->dict = NULL;
  job->check = 0;
  job->calc = new_lock(1, 1);
  job->next = NULL;
  return job;
}


void set_dictionary(job_t *prev_job, job_t *next_job, pool_t *dict_pool)
{
  if (prev_job==NULL || next_job==NULL)
    return;
  next_job->dict = get_space(dict_pool);
  assert(prev_job->in->len >= DICT);
  memcpy(next_job->dict->buf, prev_job->in->buf + (prev_job->in->len - DICT), DICT);
  next_job->dict->len = DICT;
}

int load_job(job_t *job, int input_fd)
{
  space_t *space = job->in;
  space->len = Read(input_fd, space->buf, space->size);
  return space->len;
}


void free_job (job_t *job)
{
  free(job->dict);
  free_space(job->in);
  free_space(job->out);
  free_space(job->lens);
  free(job->calc);
  free(job);
}

struct job_queue_t
{
  job_t *head;     // linked list of jobs
  job_t *tail;
  int len;         // length of job linked list
  lock_t *active;
  lock_t *use;
};

job_queue_t* new_job_queue ()
{
  job_queue_t *job_q = Malloc (sizeof(job_queue_t));
  job_q->head = NULL;
  job_q->tail = NULL;
  job_q->len = 0;
  job_q->use = new_lock (1, 1);
  job_q->active = new_lock (0, 1);
  return job_q;
}

void close_job_queue (job_queue_t *job_q)
{
  get_lock(job_q->use);
  increment_lock(job_q->active);
  release_lock(job_q->use);
}

void free_job_queue (job_queue_t *job_q)// not thread safe
{
  get_lock(job_q->use);
  while (job_q->head != NULL)
  {
    job_t *temp = job_q->head;
    job_q->head = job_q->head->next;
    free_job (temp);
  }
  release_lock(job_q->use);
}


//get a job from the beginning of the job queue
//the job should be freed if not put back to job queue after usage
job_t *get_job_bgn (job_queue_t *job_q)
{
  get_lock(job_q->active);
  get_lock(job_q->use);
  job_t *ret = job_q->head;
  if (job_q->head == NULL)
    {
      assert(job_q->len == 0);
      release_lock(job_q->use);
      return NULL;
    }
  job_q->head = job_q->head->next;
  if (job_q->head == NULL)
    job_q->tail = NULL;
  ret->next = NULL;
  --job_q->len;
  release_lock(job_q->use);
  return ret;
}

//get a job from the queue that has the same sequece number as seq
job_t* get_job_seq (job_queue_t* job_q, int seq) {

    job_t* prev = NULL;
    job_t* cur = job_q->head;

    while(1) {
        if(cur == NULL) {
            prev = NULL;
            cur = job_q->head;
        }

        if(cur->seq == seq) {
            break;
        }

        prev = cur;
        cur = cur->next;
    }

    get_lock(job_q->active);
    get_lock(job_q->use);
    if(prev == NULL) {
        job_q->head = job_q->head->next;
        if (job_q->head == NULL) {
            job_q->tail = NULL;
        }
        --job_q->len;
        cur->next = NULL;
        release_lock(job_q->use);
        return cur;
    } else {
        prev->next = cur->next;
        if (job_q->head == NULL) {
            job_q->tail = NULL;
        }
        --job_q->len;
        cur->next = NULL;
        release_lock(job_q->use);
        return cur;
    }
}

//add a job to the beginning of the job queue
void add_job_bgn (job_queue_t *job_q, job_t *job)
{
  get_lock(job_q->use);
  if (job_q->head == NULL)
    job_q->tail = job;
  job->next = job_q->head;
  job_q->head = job;
  ++job_q->len;
  increment_lock(job_q->active);
  release_lock(job_q->use);
}

//add a job to the end of the job queue
void add_job_end (job_queue_t *job_q, job_t *job)
{
  get_lock(job_q->use);
  if (job_q->tail == NULL)
  {
    job_q->head = job;
    job_q->tail = job;
    job->next = NULL;
    ++job_q->len;
  }
  job_q->tail->next = job;
  job_q->tail = job;
  job->next = NULL;
  ++job_q->len;
  increment_lock(job_q->active);
  release_lock(job_q->use);
}

#ifndef WINDOW_BITS
#  define WINDOW_BITS 15
#endif

#ifndef GZIP_ENCODING
#  define GZIP_ENCODING 16
#endif


struct compress_options {
  job_queue_t *job_queue;
  int level;
  job_queue_t *write_job_queue;
};

compress_options *new_compress_options(job_queue_t *job_queue, int level)
{
  compress_options *copts = Malloc(sizeof(compress_options));
  copts->job_queue = job_queue;
  copts->level = level;
  return copts;
}

void free_compress_options(compress_options *compress_options)
{
  free(compress_options);
}

void deflate_engine (z_stream *strm, job_t *job)
{
  int ret;
  strm->next_in = job->in->buf;
  strm->next_out = job->out->buf;
  strm->avail_in = job->in->size;
  strm->avail_out = job->out->size;
  int flush = (job->more == 0) ? Z_FINISH : Z_SYNC_FLUSH;
  ret = deflate (&strm, flush);
  assert (ret != Z_STREAM_ERROR);
  job->out->len = job->out->size - strm->avail_out;
  return;
}

// Get the next compression job from the head of the list, compress and compute
// the check value on the input, and put a job in the write list with the
// results. Keep looking for more jobs, returning when a job is found with a
// sequence number of -1 (leave that job in the list for other incarnations to
// find).

void *compress_thread(void *(opts)) {
  struct job_t *job;              // job pulled and working on
  unsigned char *next;            // pointer for blocks, check value data
  size_t left;                    // input left to process
  size_t len;                     // remaining bytes to compress/check
  int ret;                        // for error checking purposes

  compress_options* options = (compress_options *) opts;
  job_queue_t *job_queue = options->job_queue;
  int level = options->level;

  // Initialize the deflate stream
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit2 (&strm, level, Z_DEFLATED,
                          -15, 8,
                          Z_DEFAULT_STRATEGY);
  if (ret != Z_OK)
    exit (EXIT_FAILURE);

  //ret = deflateSetHeader (&strm, header);
  if (ret != Z_OK)
    exit(EXIT_FAILURE);

  // Continuously look for jobs
  for (;;) {
    // Get a job
    job = get_job_bgn(job_queue);
    assert(job != NULL);
    if (job->seq == -1)
      break;

    // Initialize and set compression level.
    (void)deflateReset(&strm);
    (void)deflateParams(&strm, level, Z_DEFAULT_STRATEGY);

    // Set dictionary if there is one
    if (job->dict != NULL) {
      deflateSetDictionary(&strm, job->dict->buf, job->dict->len);
    }

    //compress
    deflate_engine(strm, job);

    // ********** TODO **************
    // insert write job in list in sorted order, alert write thread

    // ********** TODO **************
    // calculate the check value in parallel with writing, alert the
    // write thread that the calculation is complete, and drop this
    // usage of the input buffer

    // done with that one -- go find another job

  }

  // found job with seq == -1 -- return to join
  (void)deflateEnd(&strm);
  return NULL;
}


// Write len bytes, repeating write() calls as needed. Return len.
size_t writen(int desc, void const *buf, size_t len) {
    char const *next = buf;
    size_t left = len;

    while (left) {
        size_t const max = SIZE_MAX >> 1;       // max ssize_t
        ssize_t ret = write(desc, next, left > max ? max : left);
        next += ret;
        left -= (size_t)ret;
    }
    return len;
}

unsigned put(int out, ...) {
    // compute the total number of bytes
    unsigned count = 0;
    int n;
    va_list ap;
    va_start(ap, out);
    while ((n = va_arg(ap, int)) != 0) {
        va_arg(ap, val_t);
        count += (unsigned)abs(n);
    }
    va_end(ap);

    // allocate memory for the data
    unsigned char* wrap = Malloc(count);
    //unsigned char *wrap = alloc(NULL, count);
    unsigned char *next = wrap;

    // write the requested data to wrap[]
    va_start(ap, out);
    while ((n = va_arg(ap, int)) != 0) {
        val_t val = va_arg(ap, val_t);
        if (n < 0) {            // big endian
            n = -n << 3;
            do {
                n -= 8;
                *next++ = (unsigned char)(val >> n);
            } while (n);
        }
        else                    // little endian
            do {
                *next++ = (unsigned char)val;
                val >>= 8;
            } while (--n);
    }
    va_end(ap);

    // write wrap[] to out and return the number of bytes written
    writen(out, wrap, count);
    free(wrap);
    return count;
}

length_t put_header(int outfd, char* name, time_t mtime, int level) {
    length_t len;

    len = put(outfd,
        1, (val_t)31,
        1, (val_t)139,
        1, (val_t)8,            // deflate
        1, (val_t)(name != NULL ? 8 : 0),
        4, (val_t)mtime,
        1, (val_t)(level >= 9 ? 2 : level == 1 ? 4 : 0),
        1, (val_t)3,            // unix
        0);
    if (name != NULL)
        len += writen(outfd, name, strlen(name) + 1);

    return len;
}

void put_trailer(int outfd, length_t ulen, unsigned long check) {
    put(outfd,
        4, (val_t)check,
        4, (val_t)ulen,
        0);
}



struct write_opts {
  job_queue_t *jobqueue;
  int outfd;
  char *name;
  time_t mtime;
  int level;
};

write_opts *new_write_options(job_queue_t *jobqueue, int outfd, char *name, time_t mtime, int level)
{
  write_opts *wopts = Malloc(sizeof(write_opts));
  wopts->jobqueue = jobqueue;
  wopts->outfd = outfd;
  wopts->name = name;
  wopts->mtime = mtime;
  wopts->level = level;
  return wopts;
}

void free_write_options(write_opts *write_options)
{
  free(write_options);
}


void* write_thread(void *opts) {
    struct write_opts *w_opts;
    long seq;
    struct job_queue_t *jobqueue;
    int outfd;
    char *name;
    time_t mtime;
    int level;
    struct job_t* job;
    size_t input_len;
    int more;
    length_t ulen;
    length_t clen;
    unsigned long check;

    w_opts = (struct write_opts*) opts;
    jobqueue = w_opts->jobqueue;
    outfd = w_opts->outfd;
    name = w_opts->name;
    mtime = w_opts->mtime;
    level = w_opts->level;

    put_header(outfd, name, mtime, level);

    ulen = clen = 0;
    check = crc32_z(0L, Z_NULL, 0);
    seq = 0;

    do {
        job = get_job_seq(jobqueue, seq);

        more = job->more;
        input_len = job->in->len;
        drop_space(job->in);
        ulen += input_len;
        clen += job->out->len;

        writen(outfd, job->out->buf, job->out->len);
        drop_space(job->out);

        check = crc32_z(check, (unsigned char*)(&(job->check)), input_len);
        free(job);
        seq++;
    } while (more);

    put_trailer(outfd, ulen, check);
    return NULL;
}
