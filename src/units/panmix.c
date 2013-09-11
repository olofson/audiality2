/*
 * panmix.c - Audiality 2 PanMix unit
 *
 * Copyright (C) 2012 David Olofson <david@olofson.net>
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


static inline void a2pm_process11(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	unsigned s, end = offset + frames;
	A2_panmix *pm = (A2_panmix *)u;
	int32_t *in = u->inputs[0];
	int32_t *out = u->outputs[0];
	a2_PrepareRamp(&pm->vol, frames);
	for(s = offset; s < end; ++s)
	{
		if(add)
			out[s] += (int64_t)in[s] * pm->vol.value >> 24;
		else
			out[s] = (int64_t)in[s] * pm->vol.value >> 24;
		a2_RunRamp(&pm->vol, 1);
	}
}

static void a2pm_Process11Add(A2_unit *u, unsigned offset, unsigned frames)
{
	a2pm_process11(u, offset, frames, 1);
}

static void a2pm_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	a2pm_process11(u, offset, frames, 0);
}


static inline void a2pm_process12(A2_unit *u, unsigned offset, unsigned frames,
		int add, int clamp)
{
	unsigned s, end = offset + frames;
	A2_panmix *pm = (A2_panmix *)u;
	int32_t *in = u->inputs[0];
	int32_t *out0 = u->outputs[0];
	int32_t *out1 = u->outputs[1];
/* TODO: Proper constant power panning! */
	a2_PrepareRamp(&pm->vol, frames);
	a2_PrepareRamp(&pm->pan, frames);
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
		{
			out0[s] += (int64_t)in[s] * v0 >> 24;
			out1[s] += (int64_t)in[s] * v1 >> 24;
		}
		else
		{
			out0[s] = (int64_t)in[s] * v0 >> 24;
			out1[s] = (int64_t)in[s] * v1 >> 24;
		}
		a2_RunRamp(&pm->vol, 1);
		a2_RunRamp(&pm->pan, 1);
	}
}

static void a2pm_Process12Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = (A2_panmix *)u;
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		a2pm_process12(u, offset, frames, 1, 1);
	else
		a2pm_process12(u, offset, frames, 1, 0);
}

static void a2pm_Process12(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = (A2_panmix *)u;
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		a2pm_process12(u, offset, frames, 0, 1);
	else
		a2pm_process12(u, offset, frames, 0, 0);
}

static inline void a2pm_process21(A2_unit *u, unsigned offset, unsigned frames,
		int add, int clamp)
{
	unsigned s, end = offset + frames;
	A2_panmix *pm = (A2_panmix *)u;
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[1];
	int32_t *out = u->outputs[0];
	a2_PrepareRamp(&pm->vol, frames);
	a2_PrepareRamp(&pm->pan, frames);
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
		a2_RunRamp(&pm->vol, 1);
		a2_RunRamp(&pm->pan, 1);
	}
}

static void a2pm_Process21Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = (A2_panmix *)u;
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		a2pm_process21(u, offset, frames, 1, 1);
	else
		a2pm_process21(u, offset, frames, 1, 0);
}

static void a2pm_Process21(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = (A2_panmix *)u;
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		a2pm_process21(u, offset, frames, 0, 1);
	else
		a2pm_process21(u, offset, frames, 0, 0);
}


static inline void a2pm_process22(A2_unit *u, unsigned offset, unsigned frames,
		int add, int clamp)
{
	unsigned s, end = offset + frames;
	A2_panmix *pm = (A2_panmix *)u;
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[1];
	int32_t *out0 = u->outputs[0];
	int32_t *out1 = u->outputs[1];
	a2_PrepareRamp(&pm->vol, frames);
	a2_PrepareRamp(&pm->pan, frames);
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
		{
			out0[s] += (int64_t)in0[s] * v0 >> 24;
			out1[s] += (int64_t)in1[s] * v1 >> 24;
		}
		else
		{
			out0[s] = (int64_t)in0[s] * v0 >> 24;
			out1[s] = (int64_t)in1[s] * v1 >> 24;
		}
		a2_RunRamp(&pm->vol, 1);
		a2_RunRamp(&pm->pan, 1);
	}
}

static void a2pm_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = (A2_panmix *)u;
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		a2pm_process22(u, offset, frames, 1, 1);
	else
		a2pm_process22(u, offset, frames, 1, 0);
}

static void a2pm_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = (A2_panmix *)u;
	if(pm->pan.target > 0xffffff || pm->pan.target < -0xffffff ||
			pm->pan.value > 0xffffff || pm->pan.value < -0xffffff)
		a2pm_process22(u, offset, frames, 0, 1);
	else
		a2pm_process22(u, offset, frames, 0, 0);
}


static A2_errors a2pm_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	int *ur = u->registers;
	A2_panmix *pm = (A2_panmix *)u;

	/* Internal state initialization */
	a2_SetRamp(&pm->vol, 65536, 0);
	a2_SetRamp(&pm->pan, 0, 0);

	/* Initialize VM registers */
	ur[A2PMR_VOL] = 65536;
	ur[A2PMR_PAN] = 0;

	/* Install Process callback */
	if(flags & A2_PROCADD)
		switch((u->ninputs << 1) + u->noutputs - 3)
		{
		  case 0: u->Process = a2pm_Process11Add; break;
		  case 1: u->Process = a2pm_Process12Add; break;
		  case 2: u->Process = a2pm_Process21Add; break;
		  case 3: u->Process = a2pm_Process22Add; break;
		}
	else
		switch((u->ninputs << 1) + u->noutputs - 3)
		{
		  case 0: u->Process = a2pm_Process11; break;
		  case 1: u->Process = a2pm_Process12; break;
		  case 2: u->Process = a2pm_Process21; break;
		  case 3: u->Process = a2pm_Process22; break;
		}
	return A2_OK;
}


static void a2pm_Vol(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_panmix *o = (A2_panmix *)u;
	a2_SetRamp(&o->vol, value, frames);
}

static void a2pm_Pan(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_panmix *o = (A2_panmix *)u;
	a2_SetRamp(&o->pan, value, frames);
}

static const A2_crdesc regs[] =
{
	{ "vol",	a2pm_Vol		},	/* CSPMR_VOL */
	{ "pan",	a2pm_Pan		},	/* CSPMR_PAN */
	{ NULL,	NULL				}
};


const A2_unitdesc a2_panmix_unitdesc =
{
	"panmix",		/* name */

	regs,			/* registers */

	1, A2PM_MAXINPUTS,	/* [min,max]inputs */
	1, A2PM_MAXOUTPUTS,	/* [min,max]outputs */

	sizeof(A2_panmix),	/* instancesize */
	a2pm_Initialize,	/* Initialize */
	NULL			/* Deinitialize */
};
