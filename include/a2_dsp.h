/*
 * a2_dsp.h - Handy DSP tools for Audiality 2 internals and units
 *
 * Copyright 2010-2016, 2022 David Olofson <david@olofson.net>
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

#define	A2_ONEDIV128	(0.0078125f)
#define	A2_ONEDIV256	(0.00390625f)
#define	A2_ONEDIV8K	(1.220703125e-4)
#define	A2_ONEDIV32K	(3.0517578125e-5)
#define	A2_ONEDIV65K	(1.52587890625e-5)
#define	A2_ONEDIV8M	(1.192092896e-7)
#define	A2_ONEDIV2G	(4.656612873e-10)


/*---------------------------------------------------------
	Pseudo-random numbers
---------------------------------------------------------*/

/* Returns a pseudo random number in the range [0, 65535] */
static inline int a2_IntRandom(uint32_t *nstate)
{
	*nstate *= 1566083941UL;
	(*nstate)++;
	return *nstate * (*nstate >> 16) >> 16;
}

/* Returns a pseudo random number in the range [0.0, 1.0] */
static inline float a2_Random(uint32_t *nstate)
{
	int out;
	*nstate *= 1566083941UL;
	(*nstate)++;
	out = (int)(*nstate * (*nstate >> 16) >> 16);
	return (float)(out * A2_ONEDIV65K);
}

/* Returns a pseudo random number in the range [-1.0, 1.0] */
static inline float a2_Noise(uint32_t *nstate)
{
	int out;
	*nstate *= 1566083941UL;
	(*nstate)++;
	out = (int)(*nstate * (*nstate >> 16) >> 16);
	return (float)(out - 32767) * A2_ONEDIV32K;
}


/*---------------------------------------------------------
	Interpolators
---------------------------------------------------------*/

/* Linear interpolation */
static inline float a2_Lerp(float *d, unsigned ph)
{
	int i = ph >> 8;
	float x = (ph & 0xff) * A2_ONEDIV256;
	return d[i] * (1.0f - x) + d[i + 1] * x;
}

/*
 * Cubic Hermite interpolation
 *
 *	NOTE: ph == 0 will index d[-1] through d[2]!
 */
static inline float a2_Hermite(float *d, unsigned ph)
{
#if 0
	int i = ph >> 8;
	int x = (ph & 0xff) << 7;
	int c = (d[i + 1] - d[i - 1]) >> 1;
	int a = (3 * (d[i] - d[i + 1]) + d[i + 2] - d[i - 1]) >> 1;
	int b = d[i - 1] - d[i] + c - a;
	a = a * x >> 15;
	a = (a + b) * x >> 15;
	return d[i] + ((a + c) * x >> 15);
#endif
	int i = ph >> 8;
	float x = (ph & 0xff) * A2_ONEDIV256;
	float c = (d[i + 1] - d[i - 1]) * 0.5f;
	float a = (d[i] - d[i + 1]) * 1.5f + (d[i + 2] - d[i - 1]) * 0.5f;
	float b = d[i - 1] - d[i] + c - a;
	return d[i] + (((a * x) + b) * x + c) * x;
}

/*
 * Two-stage cubic Hermite interpolation: Coefficient calculation
 *
 *	NOTE: ph == 0 will index d[-1] through d[2]!
 */
static inline void a2_Hermite2c(float *d, float *cf)
{
	cf[0] = d[0];
	cf[1] = (d[0] - d[1]) * 1.5f + (d[2] - d[-1]) * 0.5f;
	cf[3] = (d[1] - d[-1]) * 0.5f;
	cf[2] = d[-1] - d[0] + cf[3] - cf[1];
}

/* Two-stage cubic Hermite interpolation: The actual interpolation */
static inline float a2_Hermite2(int32_t *cf, unsigned ph)
{
	float x = (ph & 0xff) * A2_ONEDIV256;
	return (((cf[1] * x + cf[2]) * x) + cf[3]) * x + cf[0];
}


/*---------------------------------------------------------
	Linear ramping device
---------------------------------------------------------*/

typedef struct A2_ramper
{
	float	value;		/* Current value */
	float	target;		/* Target value */
	float	delta;		/* Per-sample delta */
	int	timer;		/* Frames to end of ramp (24:8) */
} A2_ramper;

/* Initialize an A2_ramper to a constant value of 'v' */
static inline void a2_InitRamper(A2_ramper *rr, float v)
{
	rr->value = rr->target = v;
	rr->delta = 0.0f;
	rr->timer = 0;
}

/* Prepare ramper for some processing */
static inline void a2_PrepareRamper(A2_ramper *rr, int frames)
{
	if(!rr->timer)
	{
		rr->value = rr->target;
		rr->delta = 0.0f;
	}
	else if(frames <= (rr->timer >> 8))
	{
		rr->delta = (rr->target - rr->value) / rr->timer * 256.0f;
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
 * 'target' (float) over 'duration' (24:8) sample frames.
 */
static inline void a2_SetRamper(A2_ramper *rr, float target, int start,
		int duration)
{
	rr->target = target;
	rr->timer = duration + start;
	if(rr->timer < 256)
		rr->value = rr->target;
	else
		rr->value += rr->delta * (start * A2_ONEDIV256);
}

#ifdef __cplusplus
};
#endif

#endif /* A2_DSP_H */
