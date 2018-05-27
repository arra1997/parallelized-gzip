/* utils.c

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
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include "utils.h"

ssize_t Read(int fd, void *buf, size_t count)
{
  int bytes_read;
  bytes_read = read (fd, buf, count);
  assert (bytes_read != -1);
  return bytes_read;
}

void *Calloc (size_t nelem, size_t elsize)
{
  void *addr = calloc (nelem, elsize);
  if (addr == NULL)
    {
      printf ("Insufficient memory");
      assert (addr != NULL);
    }
  return addr;
}

void *Malloc(size_t size)
{
   void *addr = malloc(size);
   if (addr == NULL)
   {
      printf ("Insufficient memory");
      assert (addr != NULL);
   }
   return addr;
}

void Unlink (const char* pathname)
{
  assert (unlink (pathname) == 0);
}
