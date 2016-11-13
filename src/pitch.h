/*
 * pitch.h - Audiality 2 pitch/frequency/rate conversion tools
 *
 * Copyright 2016 David Olofson <david@olofson.net>
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

#ifndef	A2_PITCH_INT_H
#define	A2_PITCH_INT_H

#include "a2_pitch.h"
#include "internals.h"

A2_errors a2_pitch_open(void);
void a2_pitch_close(void);

#endif	/* A2_PITCH_INT_H */
