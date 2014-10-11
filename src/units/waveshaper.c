/*
 * waveshaper.c - Audiality 2 simple waveshaper unit
 *
 *	Transfer function:
 *	  y = ( (3*a + 1) * x - (2*a * x*abs(x)) ) / (x*x * a*a + 1)
 *
 *	This waveshaper maintains what is perceived as a quite constant output
 *	power regardless of shaping amount, for input in the [-.5, .5] "0 dB"
 *	range. The downside is that output can peak around [-1.5, 1.5], which
 *	is hard to avoid while maintaining the unity transfer function when the
 *	shaping amount is 0.
 *
 * Copyright 2014 David Olofson <david@olofson.net>
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

#include "waveshaper.h"
#include "dsp.h"

#define	A2WS_MAXCHANNELS	2

/* Control register frame enumeration */
typedef enum A2WS_cregisters
{
	A2WSR_AMOUNT = 0,
	A2WSR_REGISTERS
} A2WS_cregisters;

typedef struct A2_waveshaper
{
	A2_unit		header;
	A2_ramper	amount;
} A2_waveshaper;


static inline A2_waveshaper *waveshaper_cast(A2_unit *u)
{
	return (A2_waveshaper *)u;
}


static inline void waveshaper_process(A2_unit *u, unsigned offset,
		unsigned frames, int add, int channels)
{
	A2_waveshaper *ws = waveshaper_cast(u);
	unsigned s, c, end = offset + frames;
	int32_t *in[A2WS_MAXCHANNELS], *out[A2WS_MAXCHANNELS];
	a2_RamperPrepare(&ws->amount, frames);
	for(c = 0; c < channels; ++c)
	{
		in[c] = u->inputs[c];
		out[c] = u->outputs[c];
	}
	/* NOTE: Samples are actually [-.5, .5] in 8:24 - not [-1, 1]...! */
	for(s = offset; s < end; ++s)
	{
#if 0
		/* FP prototype implementation */
		float a = ws->amount.value / (65536.0f * 256.0f);
		for(c = 0; c < channels; ++c)
		{
			float v = in[c][s] / 32768.0f / 256.0f;
			float vout = ( (3.0f * a + 1.0f) * v -
					(2.0f * a * (v * fabsf(v))) ) /
					(v * v * a * a + 1.0f);
			if(add)
				out[c][s] += vout * 32768.0f * 256.0f;
			else
				out[c][s] = vout * 32768.0f * 256.0f;
		}
#else
		/* Fixed point implementation */
		int32_t a = ws->amount.value;
		int32_t a3p1 = (a << 1) + a + (1 << 24);	// 8:24
		int32_t asqr = (int64_t)(a >> 4) * (a >> 4) >> 24; // 16:16
		for(c = 0; c < channels; ++c)
		{
			int32_t v = in[c][s];			// 9:23
			int32_t vsqr = (int64_t)v * v >> 22;	// 8:24
			int64_t vout = (int64_t)v * a3p1;	// 17:47
			int64_t sqrsub = (int64_t)a * vsqr;	// 17:47
			if(v >= 0)
				vout -= sqrsub;
			else
				vout += sqrsub;
			vout /= ((int64_t)asqr * vsqr >> 16) + (1 << 24);
			if(add)
				out[c][s] += vout;
			else
				out[c][s] = vout;
		}
#endif
		a2_RamperRun(&ws->amount, 1);
	}
}

static void waveshaper_Process11Add(A2_unit *u, unsigned offset,
		unsigned frames)
{
	waveshaper_process(u, offset, frames, 1, 1);
}

static void waveshaper_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	waveshaper_process(u, offset, frames, 0, 1);
}

static void waveshaper_Process22Add(A2_unit *u, unsigned offset,
		unsigned frames)
{
	waveshaper_process(u, offset, frames, 1, 2);
}

static void waveshaper_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	waveshaper_process(u, offset, frames, 0, 2);
}


static A2_errors waveshaper_Initialize(A2_unit *u, A2_vmstate *vms,
		A2_config *cfg, unsigned flags)
{
	A2_waveshaper *ws = waveshaper_cast(u);
	int *ur = u->registers;

	a2_RamperInit(&ws->amount, 0);

	ur[A2WSR_AMOUNT] = 0;

	if(flags & A2_PROCADD)
		switch(u->ninputs)
		{
		  case 1: u->Process = waveshaper_Process11Add; break;
		  case 2: u->Process = waveshaper_Process22Add; break;
		}
	else
		switch(u->ninputs)
		{
		  case 1: u->Process = waveshaper_Process11; break;
		  case 2: u->Process = waveshaper_Process22; break;
		}

	return A2_OK;
}


static void waveshaper_Amount(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&waveshaper_cast(u)->amount, v, start, dur);
}

static const A2_crdesc regs[] =
{
	{ "amount",	waveshaper_Amount	},	/* A2WSR_AMOUNT */
	{ NULL,	NULL				}
};


const A2_unitdesc a2_waveshaper_unitdesc =
{
	"waveshaper",		/* name */

	A2_MATCHIO,		/* flags */

	regs,			/* registers */

	1, A2WS_MAXCHANNELS,	/* [min,max]inputs */
	1, A2WS_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_waveshaper),	/* instancesize */
	waveshaper_Initialize,	/* Initialize */
	NULL			/* Deinitialize */
};
