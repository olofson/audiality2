/*
 * filter12.c - Audiality 2 12 dB/oct resonant filter unit
 *
 * Copyright 2013-2014, 2016 David Olofson <david@olofson.net>
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
#include "filter12.h"

#define	A2F12_MAXCHANNELS	2

typedef enum A2F12_cregisters
{
	A2F12R_CUTOFF = 0,
	A2F12R_Q,
	A2F12R_LP,
	A2F12R_BP,
	A2F12R_HP
} A2F12_cregisters;

typedef struct A2_filter12
{
	A2_unit		header;

	/* Needed for pitch calculations */
	int		samplerate;
	int		*transpose;

	/* Parameters */
	A2_ramper	cutoff;	/* Filter f0 (linear pitch) */
	A2_ramper	q;	/* Filter resonance */
	int		lp;	/* 24:8 fixed point */
	int		bp;
	int		hp;

	/* State */
	int		f1;	/* Current pitch coefficient */
	int		d1[A2F12_MAXCHANNELS];
	int		d2[A2F12_MAXCHANNELS];
} A2_filter12;


static inline A2_filter12 *f12_cast(A2_unit *u)
{
	return (A2_filter12 *)u;
}


static inline int f12_pitch2coeff(A2_filter12 *f12)
{
/*FIXME: Fast fixed point approximation for this... */
	float f = powf(2.0f, f12->cutoff.value * (1.0f / 65536.0f / 256.0f)) *
			A2_MIDDLEC;
	/* This filter explodes above Nyqvist / 2! (Needs oversampling...) */
	if(f > f12->samplerate >> 2)
		return 362 << 16;
	return (int)(512.0f * 65536.0f * sin(M_PI * f / f12->samplerate));
}

static inline void f12_process(A2_unit *u, unsigned offset, unsigned frames,
		int add, int channels)
{
	A2_filter12 *f12 = f12_cast(u);
	unsigned s, c, end = offset + frames;
	int32_t *in[A2F12_MAXCHANNELS], *out[A2F12_MAXCHANNELS];
	int df;
	int f0 = f12->f1;
	for(c = 0; c < channels; ++c)
	{
		in[c] = u->inputs[c];
		out[c] = u->outputs[c];
	}
	a2_PrepareRamper(&f12->q, frames);
	a2_PrepareRamper(&f12->cutoff, frames);
	if(f12->cutoff.delta)
	{
		a2_RunRamper(&f12->cutoff, frames);
		f12->f1 = f12_pitch2coeff(f12);
		df = (f12->f1 - f0 + ((int)frames >> 1)) / (int)frames;
	}
	else
		df = 0;
	for(s = offset; s < end; ++s)
	{
		int f = f0 >> 12;
		int q = f12->q.value >> 12;
		for(c = 0; c < channels; ++c)
		{
			int d1 = f12->d1[c] >> 4;
			int l = f12->d2[c] + (f * d1 >> 8);
			int h = (in[c][s] >> 5) - l - (q * d1 >> 8);
			int b = (f * (h >> 4) >> 8) + f12->d1[c];
			int fout = (l * f12->lp + b * f12->bp +
					h * f12->hp) >> 3;
			if(add)
				out[c][s] += fout;
			else
				out[c][s] = fout;
			f12->d1[c] = b;
			f12->d2[c] = l;
		}
		f0 += df;
		a2_RunRamper(&f12->q, 1);
	}
}

static void f12_Process11Add(A2_unit *u, unsigned offset, unsigned frames)
{
	f12_process(u, offset, frames, 1, 1);
}

static void f12_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	f12_process(u, offset, frames, 0, 1);
}

static void f12_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	f12_process(u, offset, frames, 1, 2);
}

static void f12_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	f12_process(u, offset, frames, 0, 2);
}

static void f12_CutOff(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_filter12 *f12 = f12_cast(u);
	a2_SetRamper(&f12->cutoff, v + *f12->transpose, start, dur);
	if(dur < 256)
		f12->f1 = f12_pitch2coeff(f12);
}

static void f12_Q(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_filter12 *f12 = f12_cast(u);
#if 1
	/* FIXME: The filter explodes at high cutoffs with the 256 limit! */
	if(v < 512)
		a2_SetRamper(&f12->q, 32768, start, dur);
#else
	if(v < 256)
		a2_SetRamper(&f12->q, 65536, start, dur);
#endif
	else
		a2_SetRamper(&f12->q, (65536 << 8) / v, start, dur);
}

static void f12_LP(A2_unit *u, int v, unsigned start, unsigned dur)
{
	f12_cast(u)->lp = v >> 8;
}

static void f12_BP(A2_unit *u, int v, unsigned start, unsigned dur)
{
	f12_cast(u)->bp = v >> 8;
}

static void f12_HP(A2_unit *u, int v, unsigned start, unsigned dur)
{
	f12_cast(u)->hp = v >> 8;
}


static A2_errors f12_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	A2_config *cfg = (A2_config *)statedata;
	A2_filter12 *f12 = f12_cast(u);
	int *ur = u->registers;
	int c;

	f12->samplerate = cfg->samplerate;
	f12->transpose = vms->r + R_TRANSPOSE;

	ur[A2F12R_CUTOFF] = 0;
	ur[A2F12R_Q] = 0;
	ur[A2F12R_LP] = 65536;
	ur[A2F12R_BP] = 0;
	ur[A2F12R_HP] = 0;

	a2_InitRamper(&f12->cutoff, 0);
	a2_InitRamper(&f12->q, 0);
	f12_CutOff(u, ur[A2F12R_CUTOFF], 0, 0);
	f12_Q(u, ur[A2F12R_Q], 0, 0);
	f12->lp = ur[A2F12R_LP] >> 8;
	f12->bp = ur[A2F12R_BP] >> 8;
	f12->hp = ur[A2F12R_HP] >> 8;

	for(c = 0; c < u->ninputs; ++c)
		f12->d1[c] = f12->d2[c] = 0;
	if(flags & A2_PROCADD)
		switch(u->ninputs)
		{
		  case 1: u->Process = f12_Process11Add; break;
		  case 2: u->Process = f12_Process22Add; break;
		}
	else
		switch(u->ninputs)
		{
		  case 1: u->Process = f12_Process11; break;
		  case 2: u->Process = f12_Process22; break;
		}

	return A2_OK;
}


static A2_errors f12_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	return A2_OK;
}


static const A2_crdesc regs[] =
{
	{ "cutoff",	f12_CutOff	},	/* A2F12_CutOff */
	{ "q",		f12_Q		},	/* A2F12_Q */
	{ "lp",		f12_LP		},	/* A2F12_LP */
	{ "bp",		f12_BP		},	/* A2F12_BP */
	{ "hp",		f12_HP		},	/* A2F12_HP */
	{ NULL,	NULL			}
};

const A2_unitdesc a2_filter12_unitdesc =
{
	"filter12",		/* name */

	A2_MATCHIO,

	regs,			/* registers */
	NULL,			/* constants */

	1, 2,			/* [min,max]inputs */
	1, 2,			/* [min,max]outputs */

	sizeof(A2_filter12),	/* instancesize */
	f12_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	f12_OpenState,		/* OpenState */
	NULL			/* CloseState */
};
