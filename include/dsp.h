/*
 * dsp.h - Handy DSP tools for Audiality 2 internals and units
 *
 * Copyright 2010-2014 David Olofson <david@olofson.net>
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

#ifndef A2_DSP_H
#define A2_DSP_H

#include "audiality2.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------
	Pseudo-random numbers
---------------------------------------------------------*/

/* Returns a pseudo random number in the range [0, 65535] */
static inline int a2_Noise(uint32_t *nstate)
{
	*nstate *= 1566083941UL;
	(*nstate)++;
	return *nstate * (*nstate >> 16) >> 16;
}


/*---------------------------------------------------------
	Interpolators for 16 bit integer data
---------------------------------------------------------*/

/* Linear interpolation */
static inline int a2_Lerp(int16_t *d, unsigned ph)
{
	int i = ph >> 8;
	int x = ph & 0xff;
	return (d[i] * (256 - x) + d[i + 1] * x) >> 8;
}

/*TODO: These could use some analysis and tweaking to maximize accuracy... */

/*
 * Cubic Hermite interpolation
 *
 *	NOTE: ph == 0 will index d[-1] through d[2]!
 */
static inline int a2_Hermite(int16_t *d, unsigned ph)
{
	int i = ph >> 8;
	int x = (ph & 0xff) << 7;
	int c = (d[i + 1] - d[i - 1]) >> 1;
	int a = (3 * (d[i] - d[i + 1]) + d[i + 2] - d[i - 1]) >> 1;
	int b = d[i - 1] - d[i] + c - a;
	a = a * x >> 15;
	a = (a + b) * x >> 15;
	return d[i] + ((a + c) * x >> 15);
}

/*
 * Two-stage cubic Hermite interpolation: Coefficient calculation
 *
 *	NOTE: ph == 0 will index d[-1] through d[2]!
 *
FIXME: Do we actually need 32 bit coefficients?
 */
static inline void a2_Hermite2c(int16_t *d, int32_t *cf)
{
	cf[0] = d[0];
	cf[1] = (3 * (d[0] - d[1]) + d[2] - d[-1]) >> 1;
	cf[3] = (d[1] - d[-1]) >> 1;
	cf[2] = d[-1] - d[0] + cf[3] - cf[1];
}

/* Two-stage cubic Hermite interpolation: The actual interpolation */
static inline int a2_Hermite2(int32_t *cf, unsigned ph)
{
	int x = (ph & 0xff) << 7;
	return ((((((cf[1] * x >> 15) +
			cf[2]) * x >> 15) +
			cf[3]) * x) >> 15) + cf[0];
}


/*---------------------------------------------------------
	8:24 control ramping device
---------------------------------------------------------*/

typedef struct A2_ramper
{
	int	value;		/* Current value (8:24) */
	int	target;		/* Target value (8:24) */
	int	delta;		/* Per-sample delta (8:24) */
	int	timer;		/* Frames to end of ramp (24:8) */
} A2_ramper;

/*
 * NOTE:
 *	Internal calculations are 8:24 fixed point, so this won't work if
 *	registers are operated outside the [-128.0, 127.0] range! This could be
 *	fixed quite easily, but it would be rather expensive on 32 bit CPUs.
 */

/* Initialize an A2_ramper to a constant value of 'v' */
static inline void a2_InitRamper(A2_ramper *rr, int v)
{
	rr->value = rr->target = v << 8;
	rr->delta = rr->timer = 0;
}

/* Prepare ramper for some processing */
static inline void a2_PrepareRamper(A2_ramper *rr, int frames)
{
	if(!rr->timer)
	{
		rr->value = rr->target;
		rr->delta = 0;
	}
	else if(frames <= (rr->timer >> 8))
	{
		rr->delta = ((int64_t)(rr->target - rr->value) << 8) / rr->timer;
		rr->timer -= frames << 8;
	}
	else
	{
		/*
		 * This isn't accurate, but the error is limited by A2_MAXFRAG,
		 * and it only ever happens with asynchronous ramps anyway!
		 */
		rr->delta = (rr->target - rr->value) / frames;
		rr->timer = 0;
	}
}

/* Advance ramper by 'frames' */
static inline void a2_RunRamper(A2_ramper *rr, int frames)
{
	rr->value += rr->delta * frames;
}

/*
 * Set up subsample accurate ramp starting at 'start' (24:8), ramping to
 * 'target' (16:16) over 'duration' (24:8) sample frames.
 */
static inline void a2_SetRamper(A2_ramper *rr, int target, int start,
		int duration)
{
	rr->target = target << 8;
	if((rr->timer = duration) < 256)
		rr->value = rr->target;
	else
		rr->value += rr->delta * start >> 8;
}

#ifdef __cplusplus
};
#endif

#endif /* A2_DSP_H */
