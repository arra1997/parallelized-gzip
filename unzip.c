/* unzip.c -- decompress files in gzip or pkzip format.

   Copyright (C) 1997-1999, 2009-2018 Free Software Foundation, Inc.
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

/*
 * The code in this file is derived from the file funzip.c written
 * and put in the public domain by Mark Adler.
 */

/*
   This version can extract files in gzip or pkzip format.
   For the latter, only the first entry is extracted, and it has to be
   either deflated or stored.
 */

#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include "tailor.h"
#include "gzip.h"
#include "inflate.h"

/* PKZIP header definitions */
#define LOCSIG 0x04034b50L      /* four-byte lead-in (lsb first) */
#define LOCFLG 6                /* offset of bit flag */
#define  CRPFLG 1               /*  bit for encrypted entry */
#define  EXTFLG 8               /*  bit for extended local header */
#define LOCHOW 8                /* offset of compression method */
/* #define LOCTIM 10               UNUSED file mod time (for decryption) */
// #define LOCCRC 14               /* offset of crc */
// #define LOCSIZ 18               /* offset of compressed size */
// #define LOCLEN 22               /* offset of uncompressed length */
#define LOCFIL 26               /* offset of file name field length */
#define LOCEXT 28               /* offset of extra field length */
#define LOCHDR 30               /* size of local header, including sig */
// #define EXTHDR 16               /* size of extended local header, inc sig */
// #define RAND_HEAD_LEN  12       /* length of encryption random header */


/* Globals */

static int decrypt;        /* flag to turn on decryption */
static int pkzip = 0;      /* set for a pkzip file */
static int ext_header = 0; /* set if extended local header */

/* ===========================================================================
 * Check zip file and advance inptr to the start of the compressed data.
 * Get ofname from the local header if necessary.
 */
int check_zipfile(in)
    int in;   /* input file descriptors */
{
    uch *h = inbuf + inptr; /* first local header */

    ifd = in;

    /* Check validity of local header, and skip name and extra fields */
    inptr += LOCHDR + SH(h + LOCFIL) + SH(h + LOCEXT);

    if (inptr > insize || LG(h) != LOCSIG) {
        fprintf(stderr, "\n%s: %s: not a valid zip file\n",
                program_name, ifname);
        exit_code = ERROR;
        return ERROR;
    }
    method = h[LOCHOW];
    if (method != STORED && method != DEFLATED) {
        fprintf(stderr,
                "\n%s: %s: first entry not deflated or stored -- use unzip\n",
                program_name, ifname);
        exit_code = ERROR;
        return ERROR;
    }

    /* If entry encrypted, decrypt and validate encryption header */
    if ((decrypt = h[LOCFLG] & CRPFLG) != 0) {
        fprintf(stderr, "\n%s: %s: encrypted file -- use unzip\n",
                program_name, ifname);
        exit_code = ERROR;
        return ERROR;
    }

    /* Save flags for unzip() */
    ext_header = (h[LOCFLG] & EXTFLG) != 0;
    pkzip = 1;

    /* Get ofname and timestamp from local header (to be done) */
    return OK;
}

/* ===========================================================================
 * Unzip in to out.  This routine works on both gzip and pkzip files.
 *
 * IN assertions: the buffer inbuf contains already the beginning of
 *   the compressed data, from offsets inptr to insize-1 included.
 *   The magic header has already been checked. The output buffer is cleared.
 */
int unzip(in, out)
    int in, out;   /* input and output file descriptors */
{
    if (in != STDIN_FILENO)
      {
        off_t ret = lseek (in, 0, SEEK_SET);
        if (ret == -1)
          exit (EXIT_FAILURE);
      }
    else
      {
        off_t ret = lseek (temp_fd, 0, SEEK_SET);
        if (ret == -1)
            exit (EXIT_FAILURE);
      }
    bytes_in = 0;
    bytes_out = 0;
    if (in != STDIN_FILENO)
        inflate_file (in, out, &bytes_in, &bytes_out);
    else
        inflate_file (temp_fd, out, &bytes_in, &bytes_out);
    inptr = insize;
    return OK;
}
