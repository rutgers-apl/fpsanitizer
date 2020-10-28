/* $Id: api.h,v 1.1 1997/08/22 01:42:26 brianp Exp $ */

/*
 * Mesa 3-D graphics library
 * Version:  2.4
 * Copyright (C) 1995-1997  Brian Paul
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/*
 * $Log: api.h,v $
 * Revision 1.1  1997/08/22 01:42:26  brianp
 * Initial revision
 *
 */


/*
 * The original api.c file has been split into two files:  api1.c and api2.c
 * because some compilers complained that api.c was too big.
 *
 * This header contains stuff only included by api1.c and api2.c
 */


#ifndef API_H
#define API_H


/*
 * Single/multiple thread context selection.
 */
#ifdef MULTI_THREADING

/* Get the context associated with the calling thread */
#define GET_CONTEXT	GLcontext *CC = gl_get_thread_context()

#else

/* CC is a global pointer for all threads in the address space */
#define GET_CONTEXT

#endif /*MULTI_THREADED*/


/*
 * Make sure there's a rendering context.
 */
#define CHECK_CONTEXT							\
   if (!CC) {								\
      if (getenv("MESA_DEBUG")) {					\
	 fprintf(stderr,"Mesa user error: no rendering context.\n");	\
      }									\
      return;								\
   }

#define CHECK_CONTEXT_RETURN(R)						\
   if (!CC) {								\
      if (getenv("MESA_DEBUG")) {					\
         fprintf(stderr,"Mesa user error: no rendering context.\n");	\
      }									\
      return (R);							\
   }


/*
 * An optimization in a few performance-critical functions.
 */
#define SHORTCUT


/*
 * Windows 95/NT DLL stuff.
 */
#ifndef WINDOWS_NT
#define APIENTRY
#endif


#endif
