/*
 * panmix.c - Audiality 2 PanMix unit
 *
 * Copyright 2012-2016, 2022 David Olofson <david@olofson.net>
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

#define	A2PM_MAXINPUTS	2
#define	A2PM_MAXOUTPUTS	2

/* Control register frame enumeration */
typedef enum A2PM_cregisters
{
	A2PMR_VOL = 0,
	A2PMR_PAN
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
	float *in = u->inputs[0];
	float *out = u->outputs[0];
	a2_PrepareRamper(&pm->vol, frames);
	for(s = offset; s < end; ++s)
	{
		if(add)
			out[s] += in[s] * pm->vol.value;
		else
			out[s] = in[s] * pm->vol.value;
		a2_RunRamper(&pm->vol, 1);
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
	float *in = u->inputs[0];
	float *out0 = u->outputs[0];
	float *out1 = u->outputs[1];
/* TODO: Proper constant power panning! */
	a2_PrepareRamper(&pm->vol, frames);
	a2_PrepareRamper(&pm->pan, frames);
	for(s = offset; s < end; ++s)
	{
		float vp = pm->pan.value * pm->vol.value;
		float v0 = pm->vol.value - vp;
		float v1 = pm->vol.value + vp;
		float ins = in[s];
		if(clamp)
		{
			if(v0 > pm->vol.value * 2.0f)
				v0 = pm->vol.value * 2.0f;
			if(v1 > pm->vol.value * 2.0f)
				v1 = pm->vol.value * 2.0f;
		}
		if(add)
		{
			out0[s] += ins * v0;
			out1[s] += ins * v1;
		}
		else
		{
			out0[s] = ins * v0;
			out1[s] = ins * v1;
		}
		a2_RunRamper(&pm->vol, 1);
		a2_RunRamper(&pm->pan, 1);
	}
}

static void panmix_Process12Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 1.0f || pm->pan.target < -1.0f ||
			pm->pan.value > 1.0f || pm->pan.value < -1.0f)
		panmix_process12(u, offset, frames, 1, 1);
	else
		panmix_process12(u, offset, frames, 1, 0);
}

static void panmix_Process12(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 1.0f || pm->pan.target < -1.0f ||
			pm->pan.value > 1.0f || pm->pan.value < -1.0f)
		panmix_process12(u, offset, frames, 0, 1);
	else
		panmix_process12(u, offset, frames, 0, 0);
}

static inline void panmix_process21(A2_unit *u, unsigned offset,
		unsigned frames, int add, int clamp)
{
	A2_panmix *pm = panmix_cast(u);
	unsigned s, end = offset + frames;
	float *in0 = u->inputs[0];
	float *in1 = u->inputs[1];
	float *out = u->outputs[0];
	a2_PrepareRamper(&pm->vol, frames);
	a2_PrepareRamper(&pm->pan, frames);
	for(s = offset; s < end; ++s)
	{
		float vp = pm->pan.value * pm->vol.value;
		float v0 = pm->vol.value - vp;
		float v1 = pm->vol.value + vp;
		if(clamp)
		{
			if(v0 > pm->vol.value * 2.0f)
				v0 = pm->vol.value * 2.0f;
			if(v1 > pm->vol.value * 2.0f)
				v1 = pm->vol.value * 2.0f;
		}
		if(add)
			out[s] += in0[s] * v0 + in1[s] * v1;
		else
			out[s] = in0[s] * v0 + in1[s] * v1;
		a2_RunRamper(&pm->vol, 1);
		a2_RunRamper(&pm->pan, 1);
	}
}

static void panmix_Process21Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 1.0f || pm->pan.target < -1.0f ||
			pm->pan.value > 1.0f || pm->pan.value < -1.0f)
		panmix_process21(u, offset, frames, 1, 1);
	else
		panmix_process21(u, offset, frames, 1, 0);
}

static void panmix_Process21(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 1.0f || pm->pan.target < -1.0f ||
			pm->pan.value > 1.0f || pm->pan.value < -1.0f)
		panmix_process21(u, offset, frames, 0, 1);
	else
		panmix_process21(u, offset, frames, 0, 0);
}


static inline void panmix_process22(A2_unit *u, unsigned offset,
		unsigned frames, int add, int clamp)
{
	A2_panmix *pm = panmix_cast(u);
	unsigned s, end = offset + frames;
	float *in0 = u->inputs[0];
	float *in1 = u->inputs[1];
	float *out0 = u->outputs[0];
	float *out1 = u->outputs[1];
	a2_PrepareRamper(&pm->vol, frames);
	a2_PrepareRamper(&pm->pan, frames);
	for(s = offset; s < end; ++s)
	{
		float vp = pm->pan.value * pm->vol.value;
		float v0 = pm->vol.value - vp;
		float v1 = pm->vol.value + vp;
		float in0s = in0[s];
		float in1s = in1[s];
		if(clamp)
		{
			if(v0 > pm->vol.value * 2.0f)
				v0 = pm->vol.value * 2.0f;
			if(v1 > pm->vol.value * 2.0f)
				v1 = pm->vol.value * 2.0f;
		}
		if(add)
		{
			out0[s] += in0s * v0;
			out1[s] += in1s * v1;
		}
		else
		{
			out0[s] = in0s * v0;
			out1[s] = in1s * v1;
		}
		a2_RunRamper(&pm->vol, 1);
		a2_RunRamper(&pm->pan, 1);
	}
}

static void panmix_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 1.0f || pm->pan.target < -1.0f ||
			pm->pan.value > 1.0f || pm->pan.value < -1.0f)
		panmix_process22(u, offset, frames, 1, 1);
	else
		panmix_process22(u, offset, frames, 1, 0);
}

static void panmix_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_panmix *pm = panmix_cast(u);
	if(pm->pan.target > 1.0f || pm->pan.target < -1.0f ||
			pm->pan.value > 1.0f || pm->pan.value < -1.0f)
		panmix_process22(u, offset, frames, 0, 1);
	else
		panmix_process22(u, offset, frames, 0, 0);
}


static A2_errors panmix_Initialize(A2_unit *u, A2_vmstate *vms,
		void *statedata, unsigned flags)
{
	A2_panmix *pm = panmix_cast(u);
	float *ur = u->registers;

	/* Internal state initialization */
	a2_InitRamper(&pm->vol, 1.0f);
	a2_InitRamper(&pm->pan, 0.0f);

	/* Initialize VM registers */
	ur[A2PMR_VOL] = 1.0f;
	ur[A2PMR_PAN] = 0.0f;

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


static void panmix_Vol(A2_unit *u, float v, unsigned start, unsigned dur)
{
	a2_SetRamper(&panmix_cast(u)->vol, v, start, dur);
}

static void panmix_Pan(A2_unit *u, float v, unsigned start, unsigned dur)
{
	a2_SetRamper(&panmix_cast(u)->pan, v, start, dur);
}


static const A2_crdesc regs[] =
{
	{ "vol",	panmix_Vol		},	/* CSPMR_VOL */
	{ "pan",	panmix_Pan		},	/* CSPMR_PAN */
	{ NULL,	NULL				}
};

static const A2_constdesc constants[] =
{
	{ "CENTER",	0.0f			},
	{ "LEFT",	-1.0f			},
	{ "RIGHT",	1.0f			},
	{ NULL,	0				}
};

const A2_unitdesc a2_panmix_unitdesc =
{
	"panmix",		/* name */

	0,			/* flags */

	regs,			/* registers */
	NULL,			/* coutputs */

	constants,		/* constants */

	1, A2PM_MAXINPUTS,	/* [min,max]inputs */
	1, A2PM_MAXOUTPUTS,	/* [min,max]outputs */

	sizeof(A2_panmix),	/* instancesize */
	panmix_Initialize,	/* Initialize */
	NULL,			/* Deinitialize */

	NULL,			/* OpenState */
	NULL			/* CloseState */
};
