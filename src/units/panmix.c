/*
 * panmix.c - Audiality 2 PanMix unit
 *
 * Copyright 2012-2013 David Olofson <david@olofson.net>
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

#include "panmix.h"
#include "dsp.h"

#define	A2PM_MAXINPUTS	2
#define	A2PM_MAXOUTPUTS	2

/* Control register frame enumeration */
typedef enum A2PM_cregisters
{
	A2PMR_VOL = 0,
	A2PMR_PAN,
	A2PMR_REGISTERS
} A2PM_cregisters;

typedef struct A2_panmix
{
	A2_unit		header;
	A2_ramper	vol;		/* Volume */
	A2_ramper	pan;		/* Horizontal pan position */
} A2_panmix;


static inline A2_panmix *panmix_cast(A2_unit *u)
{
	return (A2_panmix *)u;
}


static inline void panmix_process11(A2_unit *u, unsigned offset,
		unsigned frames, int add)
{
	A2_panmix *pm = panmix_cast(u);
	unsigned s, end = offset + frames;
	int32_t *in = u->inputs[0];
	int32_t *out = u->outputs[0];
	a2_RamperPrepare(&pm->vol, frames);
	for(s = offset; s < end; ++s)
	{
		if(add)
			out[s] += (int64_t)in[s] * pm->vol.value >> 24;
		else
			out[s] = (int64_t)in[s] * pm->vol.value >> 24;
		a2_RamperRun(&pm->vol, 1);
	}
}

static void panmix_Process11Add(A2_unit *u, unsigned offset, unsigned frames)
{
	panmix_process11(u, offset, frames, 1);
}

static void panmix_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	panmix_process11(u, offset, frames, 0);
}


static inline void panmix_process12(A2_unit *u, unsigned offset,
		unsigned frames, int add, int clamp)
{
	A2_panmix *pm = panmix_cast(u);
	unsigned s, end = offset + frames;
	int32_t *in = u->inputs[0];
	int32_t *out0 = u->outputs[0];
	int32_t *out1 = u->outputs[1];
/* TODO: Proper constant power panning! */
	a2_RamperPrepare(&pm->vol, frames);
	a2_RamperPrepare(&pm->pan, frames);
	for(s = offset; s < end; ++s)
	{
		int vp = (int64_t)pm->pan.value * pm->vol.value >> 24;
		int v0 = pm->vol.value - vp;
		int v1 = pm->vol.value + vp;
		int ins = in[s];
		if(clamp)
		{
			if(v0 > pm->vol.value << 1)
				v0 = pm->vol.value << 1;
			if(v1 > pm->vol.value << 1)
				v1 = pm->vol.value << 1;
		}
		if(add)
		{
			out0[s] += (int64_t)ins * v0 >> 24;
			out1[s] += (int64_t)ins * v1 >> 24;
		}
		else
		{
			out0[s] = (int64_t)ins * v0 >> 24;
			out1[s] = (int64_t)ins * v1 >> 24;
		}
		a2_RamperRun(&pm->vol, 1);
		a2_RamperRun(&pm->pan, 1);
	}
}

static void panmix_Process12Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		panmix_process12(u, offset, frames, 1, 1);
	else
		panmix_process12(u, offset, frames, 1, 0);
}

static void panmix_Process12(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		panmix_process12(u, offset, frames, 0, 1);
	else
		panmix_process12(u, offset, frames, 0, 0);
}

static inline void panmix_process21(A2_unit *u, unsigned offset,
		unsigned frames, int add, int clamp)
{
	A2_panmix *pm = panmix_cast(u);
	unsigned s, end = offset + frames;
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[1];
	int32_t *out = u->outputs[0];
	a2_RamperPrepare(&pm->vol, frames);
	a2_RamperPrepare(&pm->pan, frames);
	for(s = offset; s < end; ++s)
	{
		int vp = (int64_t)pm->pan.value * pm->vol.value >> 24;
		int v0 = pm->vol.value - vp;
		int v1 = pm->vol.value + vp;
		if(clamp)
		{
			if(v0 > pm->vol.value << 1)
				v0 = pm->vol.value << 1;
			if(v1 > pm->vol.value << 1)
				v1 = pm->vol.value << 1;
		}
		if(add)
			out[s] += ((int64_t)in0[s] * v0 +
					(int64_t)in1[s] * v1) >> 25;
		else
			out[s] = ((int64_t)in0[s] * v0 +
					(int64_t)in1[s] * v1) >> 25;
		a2_RamperRun(&pm->vol, 1);
		a2_RamperRun(&pm->pan, 1);
	}
}

static void panmix_Process21Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		panmix_process21(u, offset, frames, 1, 1);
	else
		panmix_process21(u, offset, frames, 1, 0);
}

static void panmix_Process21(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		panmix_process21(u, offset, frames, 0, 1);
	else
		panmix_process21(u, offset, frames, 0, 0);
}


static inline void panmix_process22(A2_unit *u, unsigned offset,
		unsigned frames, int add, int clamp)
{
	A2_panmix *pm = panmix_cast(u);
	unsigned s, end = offset + frames;
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[1];
	int32_t *out0 = u->outputs[0];
	int32_t *out1 = u->outputs[1];
	a2_RamperPrepare(&pm->vol, frames);
	a2_RamperPrepare(&pm->pan, frames);
	for(s = offset; s < end; ++s)
	{
		int vp = (int64_t)pm->pan.value * pm->vol.value >> 24;
		int v0 = pm->vol.value - vp;
		int v1 = pm->vol.value + vp;
		int in0s = in0[s];
		int in1s = in1[s];
		if(clamp)
		{
			if(v0 > pm->vol.value << 1)
				v0 = pm->vol.value << 1;
			if(v1 > pm->vol.value << 1)
				v1 = pm->vol.value << 1;
		}
		if(add)
		{
			out0[s] += (int64_t)in0s * v0 >> 24;
			out1[s] += (int64_t)in1s * v1 >> 24;
		}
		else
		{
			out0[s] = (int64_t)in0s * v0 >> 24;
			out1[s] = (int64_t)in1s * v1 >> 24;
		}
		a2_RamperRun(&pm->vol, 1);
		a2_RamperRun(&pm->pan, 1);
	}
}

static void panmix_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		panmix_process22(u, offset, frames, 1, 1);
	else
		panmix_process22(u, offset, frames, 1, 0);
}

static void panmix_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		panmix_process22(u, offset, frames, 0, 1);
	else
		panmix_process22(u, offset, frames, 0, 0);
}


static A2_errors panmix_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	A2_panmix *pm = panmix_cast(u);
	int *ur = u->registers;

	/* Internal state initialization */
	a2_RamperInit(&pm->vol, 65536);
	a2_RamperInit(&pm->pan, 0);

	/* Initialize VM registers */
	ur[A2PMR_VOL] = 65536;
	ur[A2PMR_PAN] = 0;

	/* Install Process callback */
	if(flags & A2_PROCADD)
		switch(((u->ninputs - 1) << 1) + (u->noutputs - 1))
		{
		  case 0: u->Process = panmix_Process11Add; break;
		  case 1: u->Process = panmix_Process12Add; break;
		  case 2: u->Process = panmix_Process21Add; break;
		  case 3: u->Process = panmix_Process22Add; break;
		}
	else
		switch(((u->ninputs - 1) << 1) + (u->noutputs - 1))
		{
		  case 0: u->Process = panmix_Process11; break;
		  case 1: u->Process = panmix_Process12; break;
		  case 2: u->Process = panmix_Process21; break;
		  case 3: u->Process = panmix_Process22; break;
		}
	return A2_OK;
}


static void panmix_Vol(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&panmix_cast(u)->vol, v, start, dur);
}

static void panmix_Pan(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&panmix_cast(u)->pan, v, start, dur);
}


static const A2_crdesc regs[] =
{
	{ "vol",	panmix_Vol		},	/* CSPMR_VOL */
	{ "pan",	panmix_Pan		},	/* CSPMR_PAN */
	{ NULL,	NULL				}
};

const A2_unitdesc a2_panmix_unitdesc =
{
	"panmix",		/* name */

	0,			/* flags */

	regs,			/* registers */

	1, A2PM_MAXINPUTS,	/* [min,max]inputs */
	1, A2PM_MAXOUTPUTS,	/* [min,max]outputs */

	sizeof(A2_panmix),	/* instancesize */
	panmix_Initialize,	/* Initialize */
	NULL			/* Deinitialize */
};
