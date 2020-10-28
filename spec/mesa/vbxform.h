/* $Id: vbxform.h,v 1.2 1997/04/12 16:22:22 brianp Exp $ */

/*
 * Mesa 3-D graphics library
 * Version:  2.3
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
 * $Log: vbxform.h,v $
 * Revision 1.2  1997/04/12 16:22:22  brianp
 * removed gl_init_vb()
 *
 * Revision 1.1  1997/04/02 03:14:29  brianp
 * Initial revision
 *
 */


#ifndef VBXFORM_H
#define VBXFORM_H


extern void gl_transform_vb_part1( GLcontext *ctx, GLboolean alldone );

extern void gl_transform_vb_part2( GLcontext *ctx, GLboolean alldone );


#endif
