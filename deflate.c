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

#include <zlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "utils.h"
#include "stdlib.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef WINDOW_BITS
#  define WINDOW_BITS 15
#endif

#ifndef GZIP_ENCODING
#  define GZIP_ENCODING 16
#endif

#ifndef BUFFER_SIZE
#  define BUFFER_SIZE 16384
#endif


typedef struct file_struct
{
  char *input_buffer;
  char *output_buffer;
  int level;
  int size;
  int input_size;
  int output_size;
}file_struct;


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

int deflate_file (int input_fd, int output_fd, long block_size, int level)
{
  int ret;
  int read_count;
  int write_count;
  z_stream strm;
  strm_init (&strm, level);
  unsigned char *in = Calloc (BUFFER_SIZE, sizeof (char));
  unsigned char *out = Calloc (BUFFER_SIZE, sizeof (char));
  do 
    {
      read_count = read (input_fd, in, BUFFER_SIZE);
      assert (read_count != -1);

      if (read_count > 0) 
        {
          strm.next_in = in;
          strm.next_out = out;
          strm.avail_in = read_count;
          strm.avail_out = BUFFER_SIZE;

          if (read_count < BUFFER_SIZE) 
            {
              int ret = deflate (&strm, Z_FINISH);
              assert (ret != Z_STREAM_ERROR);
              write_count = write (output_fd, out, BUFFER_SIZE - strm.avail_out);
              assert (write_count != -1);
              break;
            }

          int ret = deflate (&strm, Z_SYNC_FLUSH);
          assert (ret != Z_STREAM_ERROR);
          write_count = write (output_fd, out, BUFFER_SIZE - strm.avail_out);
          assert (write_count != -1);
        }
    }
  while (read_count > 0);

  deflateEnd (&strm);
  free (in);
  free (out);
  return 0;
}


void* compress_t(void*(f_struct_ptr))
{
  file_struct* file_struct_ptr = (file_struct*) f_struct_ptr;
  z_stream strm;
  strm_init (&strm, file_struct_ptr->level);
  strm.next_in = file_struct_ptr->input_buffer;
  strm.next_out = file_struct_ptr->output_buffer;
  strm.avail_in = file_struct_ptr->size;
  strm.avail_out = file_struct_ptr->size;
  assert(deflate(&strm, Z_FINISH) != Z_STREAM_ERROR);
  file_struct_ptr->output_size = file_struct_ptr->size - strm.avail_out;
}

int deflate_file_parallel (const char *input_file_name, int output_fd, long block_size, int level, int num_threads)
{
  int i;
  struct stat finfo;
  int fd = open(input_file_name, O_RDONLY);
  assert(fd!=-1);
  fstat(fd, &finfo);
  int filesize = finfo.st_size;
  int length_buffer = filesize/num_threads + 1;
  
  file_struct *file_structs = Calloc(num_threads, sizeof(file_struct));
  for (i = 0; i < num_threads; ++i)
    {
      file_structs[i].input_buffer = Calloc (length_buffer, sizeof(char));
      file_structs[i].output_buffer = Calloc (length_buffer, sizeof(char));
      file_structs[i].level = level;
      file_structs[i].size = length_buffer;
      int ret = read (fd, file_structs[i].input_buffer, length_buffer);
      file_structs[i].input_size = ret;
    }
  pthread_t *threads = Calloc (num_threads, sizeof(pthread_t));
  for (i = 0; i < num_threads; ++i)
    {
      pthread_create(&threads[i], NULL, compress_t, (void*)&file_structs[i]);
    }
  for (i = 0; i < num_threads; ++i)
    {
      pthread_join(threads[i], NULL);
    }
  for (i = 0; i < num_threads; ++i)
    {
      assert(write(output_fd, file_structs[i].output_buffer, file_structs[i].output_size) != -1);
      free(file_structs[i].input_buffer);
      free(file_structs[i].output_buffer);
    }
}
   
