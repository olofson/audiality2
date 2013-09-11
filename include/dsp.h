/*
 * dsp.h - Handy DSP tools for Audiality 2 internals and units
 *
 * Copyright (C) 2010-2012 David Olofson <david@olofson.net>
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
	int	value;		/* Current value */
	int	target;		/* Target value */
	int	delta;		/* Per-sample delta */
	int	frames;		/* Sample frames to end of ramp */
} A2_ramper;

/* Prepare ramper for some processing */
static inline void a2_PrepareRamp(A2_ramper *rr, int frames)
{
	if(!rr->frames)
	{
		rr->value = rr->target;
		rr->delta = 0;
	}
	else if(frames <= rr->frames)
	{
		rr->delta = (rr->target - rr->value) / rr->frames;
		rr->frames -= frames;
	}
	else
	{
		/*
		 * This isn't accurate, but the error is limited by A2_MAXFRAG,
		 * and it only ever happens with asynchronous ramps anyway!
		 */
		rr->delta = (rr->target - rr->value) / frames;
		rr->frames = 0;
	}
}

/* Advance ramper by 'frames' */
static inline void a2_RunRamp(A2_ramper *rr, int frames)
{
	rr->value += rr->delta * frames;
}

/* Set up ramp from current value to 'target' (16:16!) over 'frames' */
static inline void a2_SetRamp(A2_ramper *rr, int target, int frames)
{
	rr->target = target << 8;
	if(!(rr->frames = frames))
		rr->value = rr->target;
}

#ifdef __cplusplus
};
#endif

#endif /* A2_DSP_H */
