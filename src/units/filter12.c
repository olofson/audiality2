/*
 * filter12.c - Audiality 2 12 dB/oct resonant filter unit
 *
 * Copyright (C) 2013 David Olofson <david@olofson.net>
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
#include "dsp.h"

typedef enum A2F12_cregisters
{
	A2F12R_CUTOFF = 0,
	A2F12R_Q,
	A2F12R_LP,
	A2F12R_BP,
	A2F12R_HP,
	A2F12R_REGISTERS
} A2F12_cregisters;

typedef struct A2_filter12
{
	A2_unit		header;
	int		samplerate;

	/* Parameters */
	A2_ramper	cutoff;	/* Filter f0 (linear pitch) */
	A2_ramper	q;	/* Filter resonance */
	int		lp;	/* 24:8 fixed point */
	int		bp;
	int		hp;

	/* State */
	int		d1;
	int		d2;
	int		f1;
} A2_filter12;


static inline int a2f12_pitch2coeff(A2_filter12 *f12)
{
/*FIXME: Fast fixed point approximation for this... */
	float f = powf(2.0f, f12->cutoff.value * (1.0f / 65536.0f / 256.0f)) *
			A2_MIDDLEC;
	/* This filter explodes above Nyqvist / 4! (Needs oversampling...) */
	if(f > f12->samplerate >> 2)
		return 362 << 16;
	return (int)(512.0f * 65536.0f * sin(M_PI * f / f12->samplerate));
}

static inline void a2f12_process(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	unsigned s, end = offset + frames;
	A2_filter12 *f12 = (A2_filter12 *)u;
	int32_t *in = u->inputs[0];
	int32_t *out = u->outputs[0];
	int df;
	int f0 = f12->f1;
	a2_PrepareRamp(&f12->q, frames);
	a2_PrepareRamp(&f12->cutoff, frames);
	if(f12->cutoff.delta)
	{
		a2_RunRamp(&f12->cutoff, frames);
		f12->f1 = a2f12_pitch2coeff(f12);
		df = (f12->f1 - f0 + ((int)frames >> 1)) / (int)frames;
	}
	else
		df = 0;
	for(s = offset; s < end; ++s)
	{
		int f = f0 >> 12;
		int q = f12->q.value >> 12;
		int l = f12->d2 + (f * f12->d1 >> 12);
		int h = (in[s] >> 5) - l - (q * f12->d1 >> 12);
		int b = (f * h >> 12) + f12->d1;
		int fout = (l * f12->lp + b * f12->bp + h * f12->hp) >> 3;
		if(add)
			out[s] += fout;
		else
			out[s] = fout;
		f12->d1 = b;
		f12->d2 = l;
		f0 += df;
		a2_RunRamp(&f12->q, 1);
	}
}

static void a2f12_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	a2f12_process(u, offset, frames, 1);
}

static void a2f12_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	a2f12_process(u, offset, frames, 0);
}


static void a2f12_CutOff(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_filter12 *f12 = (A2_filter12 *)u;
	a2_SetRamp(&f12->cutoff, value + vms->r[R_TRANSPOSE], frames);
	if(!frames)
		f12->f1 = a2f12_pitch2coeff(f12);
}

static void a2f12_Q(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_filter12 *f12 = (A2_filter12 *)u;
	if(value < 256)
		a2_SetRamp(&f12->q, 65536, frames);
	else
		a2_SetRamp(&f12->q, (65536 << 8) / value, frames);
}

static void a2f12_LP(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_filter12 *f12 = (A2_filter12 *)u;
	f12->lp = value >> 8;
}

static void a2f12_BP(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_filter12 *f12 = (A2_filter12 *)u;
	f12->bp = value >> 8;
}

static void a2f12_HP(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_filter12 *f12 = (A2_filter12 *)u;
	f12->hp = value >> 8;
}

static const A2_crdesc regs[] =
{
	{ "cutoff",	a2f12_CutOff	},	/* A2F12_CutOff */
	{ "q",		a2f12_Q		},	/* A2F12_Q */
	{ "lp",		a2f12_LP	},	/* A2F12_LP */
	{ "bp",		a2f12_BP	},	/* A2F12_BP */
	{ "hp",		a2f12_HP	},	/* A2F12_HP */
	{ NULL,	NULL			}
};


static A2_errors a2f12_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	int *ur = u->registers;
	A2_filter12 *f12 = (A2_filter12 *)u;

	f12->samplerate = cfg->samplerate;

	ur[A2F12R_CUTOFF] = 0;
	ur[A2F12R_Q] = 0;
	ur[A2F12R_LP] = 65536;
	ur[A2F12R_BP] = 0;
	ur[A2F12R_HP] = 0;

	a2f12_CutOff(u, vms, ur[A2F12R_CUTOFF], 0);
	a2f12_Q(u, vms, ur[A2F12R_Q], 0);
	f12->lp = ur[A2F12R_LP] >> 8;
	f12->bp = ur[A2F12R_BP] >> 8;
	f12->hp = ur[A2F12R_HP] >> 8;

	f12->d1 = f12->d2 = 0;

	if(flags & A2_PROCADD)
		u->Process = a2f12_ProcessAdd;
	else
		u->Process = a2f12_Process;

	return A2_OK;
}


const A2_unitdesc a2_filter12_unitdesc =
{
	"filter12",		/* name */

	regs,			/* registers */

	1, 1,			/* [min,max]inputs */
	1, 1,			/* [min,max]outputs */

	sizeof(A2_filter12),	/* instancesize */
	a2f12_Initialize,	/* Initialize */
	NULL			/* Deinitialize */
};
