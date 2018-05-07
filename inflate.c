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

#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "utils.h"


#ifndef BUFFER_SIZE
#  define BUFFER_SIZE 16384
#endif

static void strm_init (z_stream *strm)
{
  int ret;
  strm->zalloc = Z_NULL;
  strm->zfree == Z_NULL;
  strm->opaque = Z_NULL;
  strm->avail_in = 0;
  strm->next_in = Z_NULL;
  ret = inflateInit(strm);
  if (ret != Z_OK)
    exit (EXIT_FAILURE);
}

int inflate_file (int input_fd, int output_fd)
{
  int ret;
  int read_count;
  int write_count;
  z_stream strm;
  strm_init (&strm);
  unsigned char *in = Calloc (BUFFER_SIZE, sizeof (char));
  unsigned char *out = Calloc (BUFFER_SIZE, sizeof (char));
  do
  {
    read_count = read (input_fd, in, BUFFER_SIZE);
    assert (read_count != -1);
    if (read_count == 0)
      break;
    strm.next_in = in;
    strm.avail_in = read_count;
    strm.next_out = out;
    strm.avail_out = BUFFER_SIZE;
    if (read_count < BUFFER_SIZE)
    {
      ret = inflate (&strm, Z_FINISH);
      assert (ret != Z_STREAM_ERROR);
      write_count = write (output_fd, out, BUFFER_SIZE - strm.avail_out);
      assert (write_count != -1);
      break;
    }
    else
    {
      ret = inflate (&strm, Z_NO_FLUSH);
      assert (ret != Z_STREAM_ERROR);
      write_count = write (output_fd, out, BUFFER_SIZE - strm.avail_out);
      assert (write_count != -1);
    }
  }
  while (read_count > 0);

  inflateEnd (&strm);
  free (in);
  free (out);
  return 0;
}
