/*
 * xinsert.h - Audiality 2 External Insert units
 *
 * Copyright (C) 2012 David Olofson <david@olofson.net>
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the
 * use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef A2_XINSERT_H
#define A2_XINSERT_H

#include "units.h"

typedef struct A2_xinsert
{
	A2_unit		header;
	A2_xinsert_cb	callback;
	void		*userdata;
	unsigned	flags;
} A2_xinsert;

extern const A2_unitdesc a2_xinsert_unitdesc;

#endif /* A2_XINSERT_H */
