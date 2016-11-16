/*
 * a2_pitch.h - Audiality 2 pitch/frequency/rate conversion tools
 *
 * Copyright 2010-2016 David Olofson <david@olofson.net>
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

/*
 *  Units:
 *	Frequency		Hz
 *	Linear Pitch		1.0/octave
 *	Phase Increment		Per-sample frame phase increment
 *
 */

#ifndef A2_PITCH_H
#define A2_PITCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Default reference frequency for linear pitch 0.0; "middle C" */
#define	A2_MIDDLEC		261.626f

/* 1000 / A2_MIDDLEC (24:40 fixed point) */
#define	A2_1K_DIV_MIDDLEC	4202608409623LL

/* Convert frequency to linear pitch (SLOW!) */
float a2_F2Pf(float f, float reference);

/* Convert linear pitch to phase increment (SLOW!) */
float a2_P2If(float pitch);

/*
 * Fast fixed point version of a2_P2I(). 'pitch' is  16:16 fixed point, and the
 * result is 8:24 fixed point.
 */
unsigned a2_P2I(int pitch);

#ifdef __cplusplus
};
#endif

#endif	/* A2_PITCH_H */
