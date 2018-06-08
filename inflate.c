/* inflate.c

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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "inflate.h"
#include "utils.h"

#ifndef WINDOW_BITS
#  define WINDOW_BITS 15
#endif

#ifndef GZIP_ENCODING
#  define GZIP_ENCODING 16
#endif

#ifndef BUFFER_SIZE_INFLATE
#  define BUFFER_SIZE_INFLATE 16384
#endif

/*
strm_init(z_stream *strm):
this function sets the necessary flags and creates the necessary structures to
call inflateInit2, which will initialize the inflate() function to allow it to
decompress .gz formatted files

strm is a structure that contains information needed to initialize the inflate
function of zlib
*/
static void strm_init (z_stream *strm)
{
  int ret;
  strm->zalloc = Z_NULL;
  strm->zfree = Z_NULL;
  strm->opaque = Z_NULL;
  ret = inflateInit2(strm, WINDOW_BITS | GZIP_ENCODING);
  if (ret != Z_OK)
    exit (EXIT_FAILURE);
}

int inflate_file (int input_fd, int output_fd, off_t *read_bytes, off_t *write_bytes)
{
  int ret;
  int flush;
  int read_count;
  int write_count;
  z_stream strm;
  strm_init (&strm);
  unsigned char *in = Calloc (BUFFER_SIZE_INFLATE, sizeof (char));
  unsigned char *out = Calloc (BUFFER_SIZE_INFLATE, sizeof (char));
  do
    {
      read_count = read (input_fd, in, BUFFER_SIZE_INFLATE);
      assert (read_count != -1);
      if (read_count == 0)
        break;
      *read_bytes += read_count;
      strm.next_in = in;
      strm.avail_in = read_count;
      flush = (read_count < BUFFER_SIZE_INFLATE) ? Z_FINISH : Z_NO_FLUSH;
      do
        {
          strm.next_out = out;
          strm.avail_out = BUFFER_SIZE_INFLATE;
          ret = inflate (&strm, flush);
          assert (ret != Z_STREAM_ERROR);
          write_count = write (output_fd, out, BUFFER_SIZE_INFLATE - strm.avail_out);
          assert (write_count != -1);
          *write_bytes += write_count;
          if (ret == Z_STREAM_END && strm.avail_in != 0)
            {
              //If meet the end of stream, initialize the new stream
              //pretend that output buffer is full to continue inflation
              unsigned char *temp_in = strm.next_in;
              int temp_avail_in = strm.avail_in;
              strm_init (&strm);
              strm.next_in = temp_in;
              strm.avail_in = temp_avail_in;
              strm.avail_out = 0; 
              continue;
            }
        }
      while (strm.avail_out == 0);
    }
  while (read_count > 0);

  inflateEnd (&strm);
  free (in);
  free (out);
  return 0;
}
