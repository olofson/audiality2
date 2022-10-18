/*
 * dcblock.c - Audiality 2 12 dB/oct DC blocker filter unit
 *
 * Copyright 2014-2016, 2022 David Olofson <david@olofson.net>
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
#include "dcblock.h"

#define	A2DCB_MAXCHANNELS	2

typedef enum A2DCB_cregisters
{
	A2DCBR_CUTOFF = 0
} A2DCB_cregisters;

typedef struct A2_dcblock
{
	A2_unit		header;

	/* Needed for pitch calculations */
	int		samplerate;
	float		*transpose;

	/* Parameters */
	float		cutoff;	/* Filter f0 (linear pitch) */

	/* State */
	float		f1;	/* Current pitch coefficient */
	float		d1[A2DCB_MAXCHANNELS];
	float		d2[A2DCB_MAXCHANNELS];
} A2_dcblock;


static inline A2_dcblock *dcb_cast(A2_unit *u)
{
	return (A2_dcblock *)u;
}

static inline float dcb_pitch2coeff(A2_dcblock *dcb)
{
	float f = a2_P2If(dcb->cutoff) * A2_MIDDLEC;
	/* This filter explodes above Nyqvist / 2! (Needs oversampling...) */
	if(f > dcb->samplerate * 0.25f)
		f = dcb->samplerate * 0.25f;
	return 2.0f * sin(M_PI * f / dcb->samplerate);
}

static inline void dcb_process(A2_unit *u, unsigned offset, unsigned frames,
		int add, int channels)
{
	A2_dcblock *dcb = dcb_cast(u);
	unsigned s, c, end = offset + frames;
	float *in[A2DCB_MAXCHANNELS], *out[A2DCB_MAXCHANNELS];
	float f = dcb->f1;
	for(c = 0; c < channels; ++c)
	{
		in[c] = u->inputs[c];
		out[c] = u->outputs[c];
	}
	for(s = offset; s < end; ++s)
	{
		for(c = 0; c < channels; ++c)
		{
			float d1 = dcb->d1[c];
			float low = dcb->d2[c] + f * d1;
			float high = in[c][s] * 0.5f - low - d1;
			float band = f * high + dcb->d1[c];
			if(add)
				out[c][s] += high;
			else
				out[c][s] = high;
			dcb->d1[c] = band;
			dcb->d2[c] = low;
		}
	}
}

static void dcb_Process11Add(A2_unit *u, unsigned offset, unsigned frames)
{
	dcb_process(u, offset, frames, 1, 1);
}

static void dcb_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	dcb_process(u, offset, frames, 0, 1);
}

static void dcb_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	dcb_process(u, offset, frames, 1, 2);
}

static void dcb_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	dcb_process(u, offset, frames, 0, 2);
}

static void dcb_CutOff(A2_unit *u, float v, unsigned start, unsigned dur)
{
	A2_dcblock *dcb = dcb_cast(u);
	dcb->cutoff = v + *dcb->transpose;
	dcb->f1 = dcb_pitch2coeff(dcb);
}

static A2_errors dcb_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	A2_config *cfg = (A2_config *)statedata;
	A2_dcblock *dcb = dcb_cast(u);
	float *ur = u->registers;
	int c;

	dcb->samplerate = cfg->samplerate;
	dcb->transpose = vms->r + R_TRANSPOSE;

	ur[A2DCBR_CUTOFF] = -5.0f;	/* 8.175813 Hz */
	dcb_CutOff(u, ur[A2DCBR_CUTOFF], 0, 0);

	for(c = 0; c < u->ninputs; ++c)
		dcb->d1[c] = dcb->d2[c] = 0.0f;
	if(flags & A2_PROCADD)
		switch(u->ninputs)
		{
		  case 1: u->Process = dcb_Process11Add; break;
		  case 2: u->Process = dcb_Process22Add; break;
		}
	else
		switch(u->ninputs)
		{
		  case 1: u->Process = dcb_Process11; break;
		  case 2: u->Process = dcb_Process22; break;
		}

	return A2_OK;
}


static A2_errors dcb_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	return A2_OK;
}


static const A2_crdesc regs[] =
{
	{ "cutoff",	dcb_CutOff	},	/* A2DCB_CutOff */
	{ NULL,	NULL			}
};

const A2_unitdesc a2_dcblock_unitdesc =
{
	"dcblock",		/* name */

	A2_MATCHIO,

	regs,			/* registers */
	NULL,			/* coutputs */

	NULL,			/* constants */

	1, 2,			/* [min,max]inputs */
	1, 2,			/* [min,max]outputs */

	sizeof(A2_dcblock),	/* instancesize */
	dcb_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */


	dcb_OpenState,		/* OpenState */
	NULL			/* CloseState */
};
