/*
 * dcblock.c - Audiality 2 12 dB/oct DC blocker filter unit
 *
 * Copyright 2014-2016 David Olofson <david@olofson.net>
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
	A2DCBR_CUTOFF = 0,
	A2DCBR_REGISTERS
} A2DCB_cregisters;

typedef struct A2_dcblock
{
	A2_unit		header;

	/* Needed for pitch calculations */
	int		samplerate;
	int		*transpose;

	/* Parameters */
	int		cutoff;	/* Filter f0 (linear pitch) */

	/* State */
	int		f1;	/* Current pitch coefficient */
	int		d1[A2DCB_MAXCHANNELS];
	int		d2[A2DCB_MAXCHANNELS];
} A2_dcblock;


static inline A2_dcblock *dcb_cast(A2_unit *u)
{
	return (A2_dcblock *)u;
}

static inline int dcb_pitch2coeff(A2_dcblock *dcb)
{
	float f = powf(2.0f, dcb->cutoff * (1.0f / 65536.0f)) * A2_MIDDLEC;
	/* This filter explodes above Nyqvist / 2! (Needs oversampling...) */
	if(f > dcb->samplerate >> 2)
		return 362 << 16;
	return (int)(512.0f * 65536.0f * sin(M_PI * f / dcb->samplerate));
}

static inline void dcb_process(A2_unit *u, unsigned offset, unsigned frames,
		int add, int channels)
{
	A2_dcblock *dcb = dcb_cast(u);
	unsigned s, c, end = offset + frames;
	int32_t *in[A2DCB_MAXCHANNELS], *out[A2DCB_MAXCHANNELS];
	int f = dcb->f1 >> 12;
	for(c = 0; c < channels; ++c)
	{
		in[c] = u->inputs[c];
		out[c] = u->outputs[c];
	}
	for(s = offset; s < end; ++s)
	{
		for(c = 0; c < channels; ++c)
		{
			int d1 = dcb->d1[c] >> 4;
			int l = dcb->d2[c] + (f * d1 >> 8);
			int h = (in[c][s] >> 5) - l - (d1 << 4);
			int b = (f * (h >> 4) >> 8) + dcb->d1[c];
			int fout = h << 5;
			if(add)
				out[c][s] += fout;
			else
				out[c][s] = fout;
			dcb->d1[c] = b;
			dcb->d2[c] = l;
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

static void dcb_CutOff(A2_unit *u, int v, unsigned start, unsigned dur)
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
	int *ur = u->registers;
	int c;

	dcb->samplerate = cfg->samplerate;
	dcb->transpose = vms->r + R_TRANSPOSE;

	ur[A2DCBR_CUTOFF] = -5 << 16;	/* 8.175813 Hz */
	dcb_CutOff(u, ur[A2DCBR_CUTOFF], 0, 0);

	for(c = 0; c < u->ninputs; ++c)
		dcb->d1[c] = dcb->d2[c] = 0;
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
	NULL,			/* constants */

	1, 2,			/* [min,max]inputs */
	1, 2,			/* [min,max]outputs */

	sizeof(A2_dcblock),	/* instancesize */
	dcb_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */


	dcb_OpenState,		/* OpenState */
	NULL			/* CloseState */
};
