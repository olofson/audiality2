/*
 * limiter.c - Audiality 2 compressor/limiter unit
 *
 * Copyright 2001-2002, 2009, 2012, 2016, 2022 David Olofson <david@olofson.net>
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

#include <math.h>
#include "limiter.h"

#define	A2L_MAXCHANNELS	2

/* Control register frame enumeration */
typedef enum A2L_cregisters
{
	A2LR_RELEASE = 0,
	A2LR_THRESHOLD
} A2L_cregisters;

typedef struct A2_limiter
{
	A2_unit		header;
	int		samplerate;
	float		threshold;	/* Reaction threshold */
	float		release;	/* Release "speed" */
	float		peak;		/* Filtered peak value */
} A2_limiter;


static inline A2_limiter *limiter_cast(A2_unit *u)
{
	return (A2_limiter *)u;
}


static inline void limiter_process11(A2_unit *u, unsigned offset,
		unsigned frames, int add)
{
	A2_limiter *lim = limiter_cast(u);
	unsigned s, end = offset + frames;
	float *in = u->inputs[0];
	float *out = u->outputs[0];
	for(s = offset; s < end; ++s)
	{
		int gain;
		float p = fabs(in[s]);
		if(p > lim->peak)
			lim->peak = p;
		else
		{
			lim->peak -= lim->release;
			if(lim->peak < lim->threshold)
				lim->peak = lim->threshold;
			p = lim->peak;
		}
		gain = 1.0f / p;
		if(add)
			out[s] += in[s] * gain;
		else
			out[s] = in[s] * gain;
	}
}

static void limiter_Process11Add(A2_unit *u, unsigned offset, unsigned frames)
{
	limiter_process11(u, offset, frames, 1);
}

static void limiter_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	limiter_process11(u, offset, frames, 0);
}


/*
 * Smart Stereo version.
 *
 * This algorithm takes both channels in account in a way
 * that reduces the effect of the center appearing to have
 * more power after compression of signals with unbalanced
 * stereo images.
 *
 * A dead center signal can only get 3 dB louder than the
 * same signal in one channel only, as opposed to the
 * normal 6 dB of a limiter that only looks at max(L, R).
 *
 * Meanwhile, with "normal" material (ie most power
 * relatively centered), this limiter gets an extra 3 dB
 * compared to a limiter that checks (L+R).
 */
static inline void limiter_process22(A2_unit *u, unsigned offset,
		unsigned frames, int add)
{
	A2_limiter *lim = limiter_cast(u);
	unsigned s, end = offset + frames;
	float *in0 = u->inputs[0];
	float *in1 = u->inputs[1];
	float *out0 = u->outputs[0];
	float *out1 = u->outputs[1];
	for(s = offset; s < end; ++s)
	{
		float gain;
		float lp = fabs(in0[s]);
		float rp = fabs(in1[s]);
		float p = lp > rp ? lp : rp;
		p = p + 0.5f * (p - fabs(lp - rp));
		if(p > lim->peak)
			lim->peak = p;
		else
		{
			lim->peak -= lim->release;
			if(lim->peak < lim->threshold)
				lim->peak = lim->threshold;
			p = lim->peak;
		}
		gain = 1.0f / p;
		if(add)
		{
			out0[s] += in0[s] * gain;
			out1[s] += in1[s] * gain;
		}
		else
		{
			out0[s] = in0[s] * gain;
			out1[s] = in1[s] * gain;
		}
	}
}

static void limiter_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	limiter_process22(u, offset, frames, 1);
}

static void limiter_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	limiter_process22(u, offset, frames, 0);
}


static A2_errors limiter_Initialize(A2_unit *u, A2_vmstate *vms,
		void *statedata, unsigned flags)
{
	A2_config *cfg = (A2_config *)statedata;
	A2_limiter *lim = limiter_cast(u);
	float *ur = u->registers;

	ur[A2LR_RELEASE] = 64.0f;
	ur[A2LR_THRESHOLD] = 1.0f;

	lim->samplerate = cfg->samplerate;
	lim->release = ur[A2LR_RELEASE] / cfg->samplerate;
	lim->threshold = ur[A2LR_THRESHOLD];
	lim->peak = 1.0f;

	if(flags & A2_PROCADD)
		switch(u->ninputs)
		{
		  case 1: u->Process = limiter_Process11Add; break;
		  case 2: u->Process = limiter_Process22Add; break;
		}
	else
		switch(u->ninputs)
		{
		  case 1: u->Process = limiter_Process11; break;
		  case 2: u->Process = limiter_Process22; break;
		}

	return A2_OK;
}


static void limiter_Release(A2_unit *u, float v, unsigned start, unsigned dur)
{
	A2_limiter *lim = limiter_cast(u);
	lim->release = v / lim->samplerate;
}

static void limiter_Threshold(A2_unit *u, float v, unsigned start, unsigned dur)
{
	A2_limiter *lim = limiter_cast(u);
	lim->threshold = v;
	if(lim->threshold < 0.001f)
		lim->threshold = 0.001f;
}


static A2_errors limiter_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	return A2_OK;
}


static const A2_crdesc regs[] =
{
	{ "release",	limiter_Release		},	/* A2LR_RELEASE */
	{ "threshold",	limiter_Threshold	},	/* A2LR_THRESHOLD */
	{ NULL,	NULL				}
};

const A2_unitdesc a2_limiter_unitdesc =
{
	"limiter",		/* name */

	A2_MATCHIO,		/* flags */

	regs,			/* registers */
	NULL,			/* coutputs */

	NULL,			/* constants */

	1, A2L_MAXCHANNELS,	/* [min,max]inputs */
	1, A2L_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_limiter),	/* instancesize */
	limiter_Initialize,	/* Initialize */
	NULL,			/* Deinitialize */

	limiter_OpenState,	/* OpenState */
	NULL			/* CloseState */
};
