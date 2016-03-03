/*
 * limiter.c - Audiality 2 compressor/limiter unit
 *
 * Copyright 2001-2002, 2009, 2012, 2016 David Olofson <david@olofson.net>
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

#include <stdlib.h>
#include "limiter.h"

#define	A2L_MAXCHANNELS	2

/* Control register frame enumeration */
typedef enum A2L_cregisters
{
	A2LR_RELEASE = 0,
	A2LR_THRESHOLD,
	A2LR_REGISTERS
} A2L_cregisters;

typedef struct A2_limiter
{
	A2_unit		header;
	int		samplerate;
	unsigned	threshold;	/* Reaction threshold */
	int		release;	/* Release "speed" */
	unsigned	peak;		/* Filtered peak value */
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
	int32_t *in = u->inputs[0];
	int32_t *out = u->outputs[0];
	for(s = offset; s < end; ++s)
	{
		int gain;
		unsigned p = (unsigned)abs(in[s]);
		if(p > lim->peak)
			lim->peak = p;
		else
		{
			lim->peak -= lim->release;
			if(lim->peak < lim->threshold)
				lim->peak = lim->threshold;
			p = lim->peak;
		}
		gain = (32767LL << 16) / ((p + 511) >> 9);
		if(add)
			out[s] += (int64_t)in[s] * gain >> 16;
		else
			out[s] = (int64_t)in[s] * gain >> 16;
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
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[1];
	int32_t *out0 = u->outputs[0];
	int32_t *out1 = u->outputs[1];
	for(s = offset; s < end; ++s)
	{
		int gain;
		int lp = abs(in0[s]);
		int rp = abs(in1[s]);
		unsigned p = (unsigned)(lp > rp ? lp : rp);
		p = p + ((p - abs(lp - rp)) >> 1);
		if(p > lim->peak)
			lim->peak = p;
		else
		{
			lim->peak -= lim->release;
			if(lim->peak < lim->threshold)
				lim->peak = lim->threshold;
			p = lim->peak;
		}
		gain = (32767LL << 16) / ((p + 511) >> 9);
		if(add)
		{
			out0[s] += (int64_t)in0[s] * gain >> 16;
			out1[s] += (int64_t)in1[s] * gain >> 16;
		}
		else
		{
			out0[s] = (int64_t)in0[s] * gain >> 16;
			out1[s] = (int64_t)in1[s] * gain >> 16;
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
	int *ur = u->registers;

	ur[A2LR_RELEASE] = 64 << 16;
	ur[A2LR_THRESHOLD] = 1 << 16;

	lim->samplerate = cfg->samplerate;
	lim->release = (ur[A2LR_RELEASE] << 8) / cfg->samplerate;
	lim->threshold = (unsigned)(ur[A2LR_THRESHOLD] << 8);
	lim->peak = 32768 << 8;

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


static void limiter_Release(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_limiter *lim = limiter_cast(u);
	lim->release = (v << 8) / lim->samplerate;
}

static void limiter_Threshold(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_limiter *lim = limiter_cast(u);
	lim->threshold = (unsigned)(v << 8);
	if(lim->threshold < 256)
		lim->threshold = 256;
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
	NULL,			/* constants */

	1, A2L_MAXCHANNELS,	/* [min,max]inputs */
	1, A2L_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_limiter),	/* instancesize */
	limiter_Initialize,	/* Initialize */
	NULL,			/* Deinitialize */

	limiter_OpenState,	/* OpenState */
	NULL			/* CloseState */
};
