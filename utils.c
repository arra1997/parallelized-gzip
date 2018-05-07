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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

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

void Unlink (const char* pathname)
{
  assert (unlink (pathname) == 0);
}