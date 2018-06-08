/* zip.c -- compress files to the gzip or pkzip format

   Copyright (C) 1997-1999, 2006-2007, 2009-2018 Free Software Foundation, Inc.
   Copyright (C) 1992-1993 Jean-loup Gailly

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
#include <ctype.h>
#include <zlib.h>

#include "deflate.h"
#include "tailor.h"
#include "gzip.h"

local ulg crc;       /* crc on uncompressed file data */
off_t header_bytes;   /* number of bytes in gzip header */

/* ===========================================================================
 * Deflate in to out.
 * IN assertions: the input and output buffers are cleared.
 *   The variables time_stamp and save_orig_name are initialized.
 */
int zip(in, out)
    int in, out;            /* input and output file descriptors */
{
    method = DEFLATED;
    if (time_stamp.tv_nsec < 0)
      ;
    else if (0 < time_stamp.tv_sec && time_stamp.tv_sec <= 0xffffffff)
      ;
    else
      {
        warning ("file timestamp out of range for gzip format");
      }

    // gz_header header =
    //   {
    //     .text = attr,
    //     .time = stamp,
    //     .xflags = 0,
    //     .os = OS_CODE,
    //     .extra = Z_NULL,
    //     .extra_len = 0,
    //     .extra_max = 0,
    //     .name = Z_NULL,
    //     .name_max = 0,
    //     .comment = Z_NULL,
    //     .comm_max = 0,
    //     .hcrc = 0,
    //     .done = 0
    //   };
    // header_bytes = 10;
    if (save_orig_name)
      {
        Bytef *p = (Bytef*) gzip_base_name (ifname);
        //header.name = p;
        do
          {
            header_bytes++;
          }
        while (*p++);
      }
    bytes_in = 0;
    bytes_out = 0;
    //deflate_file (in, out, 128*128, level, &header, &bytes_in, &bytes_out);
    //header_bytes += 2*4;

    char name[16] = "compressed_file";
    deflate_file_parallel(in, out, 1024*128, processes, level, name, 0);
    return OK;
}


/* ===========================================================================
 * Read a new buffer from the current input file, perform end-of-line
 * translation, and update the crc and input file size.
 * IN assertion: size >= 2 (for end-of-line translation)
 */
int file_read(buf, size)
    char *buf;
    unsigned size;
{
    unsigned len;

    Assert(insize == 0, "inbuf not empty");

    len = read_buffer (ifd, buf, size);
    if (len == 0) return (int)len;
    if (len == (unsigned)-1) {
        read_error();
        return EOF;
    }

    crc = updcrc((uch*)buf, len);
    bytes_in += (off_t)len;
    return (int)len;
}
