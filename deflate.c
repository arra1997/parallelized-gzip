/* deflate.c

   Copyright (C) 2018 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <config.h>
#include <zlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "deflate.h"
#include "utils.h"
#include "stdlib.h"
#include "deflate.h"
#include "parallel.h"

#define DICT 32768U

#ifndef WINDOW_BITS
#  define WINDOW_BITS 15
#endif

#ifndef GZIP_ENCODING
#  define GZIP_ENCODING 16
#endif

#ifndef BUFFER_SIZE_DEFLATE
#  define BUFFER_SIZE_DEFLATE 16384
#endif



/*
strm_init(z_stream *strm, int level):
this function sets the necessary flags and creates the necessary structures to
call deflateInit2, which will initialize the deflate() function to allow it to
compress files in gzip format into .gz files

strm is a structure that contains information needed to initialize the inflate
function of zlib

level is the compression level that the algorithm will compress at, higher is better
but slower
*/
static void strm_init (z_stream *strm, int level)
{
    strm->zalloc = Z_NULL;
    strm->zfree  = Z_NULL;
    strm->opaque = Z_NULL;
    int ret = deflateInit2 (strm, level, Z_DEFLATED,
                            WINDOW_BITS | GZIP_ENCODING, 8,
                            Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
      exit (EXIT_FAILURE);
}


int deflate_file_parallel (int input_fd, int output_fd, long block_size,
			   int processes, int level, char *name, time_t mtime)
{
  //Initialize job queue and memory pools
  int i;
  unsigned long seq;
  job_queue_t *job_queue;
  job_queue_t *write_job_queue;
  job_t *prev_job, *job;
  pool_t *input_pool, *output_pool, *dict_pool;
  compress_options *c_opts;
  write_opts *w_opts;
  pthread_t *pthread_array;

  job_queue = new_job_queue ();
  write_job_queue = new_job_queue ();
  input_pool = new_pool (block_size, 2*processes);
  output_pool = new_pool (block_size, 2*processes);
  dict_pool = new_pool (DICT, 2*processes);
  seq = 0;
  prev_job = job = NULL;

  //Create processes # of new threads for compression and 1 for writing
  pthread_array = Calloc (processes + 1, sizeof(pthread_array));
  c_opts = new_compress_options (job_queue, write_job_queue, level);
  w_opts = new_write_options (write_job_queue, output_fd, name, mtime, level);

  for (i = 0; i < processes; ++i)
    {
      pthread_create(&pthread_array[i], NULL, compress_thread, (void*) c_opts);
    }
  pthread_create(&pthread_array[i], NULL, write_thread, (void*) w_opts);


  // Populate jobs add to job queue
  while(1)
    {
      job = new_job (seq, input_pool, output_pool, NULL);

      if (load_job (job, input_fd)  == 0)
	    {
    	  if (prev_job != NULL)
    	    {
    	      set_last_job (prev_job);
    	      add_job_end (job_queue, prev_job);
    	    }
    	  free_job (job);
    	  break;
	    }

      else if (prev_job != NULL)
    	{
    	  set_dictionary (prev_job, job, dict_pool);
    	  add_job_end (job_queue, prev_job);
    	}

      prev_job = job;
      ++seq;
    }
  close_job_queue (job_queue);


  for (i = 0; i < processes + 1; ++i)
    {
      pthread_join (pthread_array[i], NULL);
    }

  free_pool (input_pool);
  free_pool (output_pool);
  free_pool (dict_pool);
  free_job_queue (job_queue);
  free_job_queue (write_job_queue);
  free_compress_options (c_opts);
  free_write_options (w_opts);
  free (pthread_array);
  return 0;
}


int deflate_file (int input_fd, int output_fd, long block_size, int level,
  gz_header *header, off_t *read_bytes, off_t *write_bytes)
{
  int read_count;
  int write_count;
  z_stream strm;
  strm_init (&strm, level);
  int ret = deflateSetHeader (&strm, header);
  if (ret != Z_OK)
    exit (EXIT_FAILURE);
  unsigned char *in = Calloc (BUFFER_SIZE_DEFLATE, sizeof (char));
  unsigned char *out = Calloc (BUFFER_SIZE_DEFLATE, sizeof (char));
  do
    {
      read_count = read (input_fd, in, BUFFER_SIZE_DEFLATE);
      assert (read_count != -1);
      *read_bytes += read_count;

      if (read_count > 0)
        {
          strm.next_in = in;
          strm.next_out = out;
          strm.avail_in = read_count;
          strm.avail_out = BUFFER_SIZE_DEFLATE;

          //if we're at the end of the file, compress the last chunk using the Z_FINISH flag
          if (read_count < BUFFER_SIZE_DEFLATE)
            {
              int ret = deflate (&strm, Z_FINISH);
              assert (ret != Z_STREAM_ERROR);
              write_count = write (output_fd, out, BUFFER_SIZE_DEFLATE - strm.avail_out);
              assert (write_count != -1);
              *write_bytes += write_count;
              break;
            }

          //if not at the end of file, compress each chunk normally, using Z_SYNC_FLUSH
          int ret = deflate (&strm, Z_SYNC_FLUSH);
          assert (ret != Z_STREAM_ERROR);
          write_count = write (output_fd, out, BUFFER_SIZE_DEFLATE - strm.avail_out);
          assert (write_count != -1);
          *write_bytes += write_count;
        }
    }
  while (read_count > 0);

  deflateEnd (&strm);
  free (in);
  free (out);
  return 0;
}
