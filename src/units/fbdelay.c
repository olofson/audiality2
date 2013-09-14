/*
 * fbdelay.c - Audiality 2 feedback delay unit
 *
 * Copyright 2013 David Olofson <david@olofson.net>
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
#include "fbdelay.h"

/* Control register frame enumeration */
typedef enum A2FBD_cregisters
{
	A2FBDR_FBDELAY = 0,
	A2FBDR_LDELAY,
	A2FBDR_RDELAY,
	A2FBDR_DRYGAIN,
	A2FBDR_FBGAIN,
	A2FBDR_LGAIN,
	A2FBDR_RGAIN,
	A2FBDR_REGISTERS
} A2FBD_cregisters;

typedef struct A2_fbdelay
{
	A2_unit		header;
	int		samplerate;

	/* Parameters */
	int		fbdelay;	/* Timings (sample frames) */
	int		ldelay;
	int		rdelay;
	int		drygain;	/* Gains (16:16 fixp) */
	int		fbgain;
	int		lgain;
	int		rgain;

	/* Delay buffers */
	int32_t		*lbuf;
	int32_t		*rbuf;
	int		bufpos;
} A2_fbdelay;


#define	WI(x)	((fbd->bufpos - (x)) & 0xffff)
static inline void a2fbd_process22(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	unsigned s, end = offset + frames;
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	int32_t	*b0 = fbd->lbuf;
	int32_t	*b1 = fbd->rbuf;
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[1];
	int32_t *out0 = u->outputs[0];
	int32_t *out1 = u->outputs[1];
	for(s = offset; s < end; ++s)
	{
		int i0 = in0[s];
		int i1 = in1[s];

		/* Feedback delay taps (NOTE: Reverse stereo!) */
		int o0 = (int64_t)b1[WI(fbd->fbdelay)] * fbd->fbgain >> 16;
		int o1 = (int64_t)b0[WI(fbd->fbdelay)] * fbd->fbgain >> 16;

		/* Inject input + feedback into the buffers */
		b0[WI(0)] = i0 + o0;
		b1[WI(0)] = i1 + o1;

		/* Delay taps */
		o0 += (int64_t)b0[WI(fbd->ldelay)] * fbd->lgain >> 16;
		o1 += (int64_t)b1[WI(fbd->rdelay)] * fbd->rgain >> 16;

		/* Dry bypass */
		o0 += (int64_t)i0 * fbd->drygain >> 16;
		o1 += (int64_t)i1 * fbd->drygain >> 16;

		/* Output */
		if(add)
		{
			out0[s] += o0;
			out1[s] += o1;
		}
		else
		{
			out0[s] = o0;
			out1[s] = o1;
		}
		++fbd->bufpos;
	}
}
#undef	WI

static void a2fbd_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	a2fbd_process22(u, offset, frames, 1);
}

static void a2fbd_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	a2fbd_process22(u, offset, frames, 0);
}


static A2_errors a2fbd_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	int *ur = u->registers;
	A2_fbdelay *fbd = (A2_fbdelay *)u;

	fbd->samplerate = cfg->samplerate;

/*FIXME: Select buffer size based on sample rate and maximum delay allowed! */
	fbd->lbuf = calloc(65536, sizeof(int32_t));
	fbd->rbuf = calloc(65536, sizeof(int32_t));
	if(!fbd->lbuf || !fbd->rbuf)
	{
/*FIXME: Use realtime safe memory manager! */
		free(fbd->lbuf);
		free(fbd->rbuf);
		return A2_OOMEMORY;
	}
	fbd->bufpos = 0;

	ur[A2FBDR_FBDELAY] = 400 << 16;
	ur[A2FBDR_LDELAY] = 280 << 16;
	ur[A2FBDR_RDELAY] = 320 << 16;
	fbd->fbdelay = (int64_t)ur[A2FBDR_FBDELAY] * fbd->samplerate / 65536000;
	fbd->ldelay = (int64_t)ur[A2FBDR_LDELAY] * fbd->samplerate / 65536000;
	fbd->rdelay = (int64_t)ur[A2FBDR_RDELAY] * fbd->samplerate / 65536000;
	fbd->drygain = ur[A2FBDR_DRYGAIN] = 65536;
	fbd->fbgain = ur[A2FBDR_FBGAIN] = 16384;
	fbd->lgain = ur[A2FBDR_LGAIN] = 32768;
	fbd->rgain = ur[A2FBDR_RGAIN] = 32768;

	if(flags & A2_PROCADD)
		u->Process = a2fbd_Process22Add;
	else
		u->Process = a2fbd_Process22;

	return A2_OK;
}

static void a2fbd_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
/*FIXME: Use realtime safe memory manager! */
	free(fbd->lbuf);
	free(fbd->rbuf);
}


static void a2fbd_FBDelay(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->fbdelay = (int64_t)value * fbd->samplerate / 65536000;
}

static void a2fbd_LDelay(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->ldelay = (int64_t)value * fbd->samplerate / 65536000;
}

static void a2fbd_RDelay(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->rdelay = (int64_t)value * fbd->samplerate / 65536000;
}

static void a2fbd_DryGain(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->drygain = value;
}

static void a2fbd_FBGain(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->fbgain = value;
}

static void a2fbd_LGain(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->lgain = value;
}

static void a2fbd_RGain(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_fbdelay *fbd = (A2_fbdelay *)u;
	fbd->rgain = value;
}

static const A2_crdesc regs[] =
{
	{ "fbdelay",	a2fbd_FBDelay		},	/* A2FBDR_FBDELAY */
	{ "ldelay",	a2fbd_LDelay		},	/* A2FBDR_LDELAY */
	{ "rdelay",	a2fbd_RDelay		},	/* A2FBDR_RDELAY */
	{ "drygain",	a2fbd_DryGain		},	/* A2FBDR_DRYGAIN */
	{ "fbgain",	a2fbd_FBGain		},	/* A2FBDR_FBGAIN */
	{ "lgain",	a2fbd_LGain		},	/* A2FBDR_LGAIN */
	{ "rgain",	a2fbd_RGain		},	/* A2FBDR_RGAIN */
	{ NULL,	NULL				}
};


const A2_unitdesc a2_fbdelay_unitdesc =
{
	"fbdelay",		/* name */

	regs,			/* registers */

	2, 2,			/* [min,max]inputs */
	2, 2,			/* [min,max]outputs */

	sizeof(A2_fbdelay),	/* instancesize */
	a2fbd_Initialize,	/* Initialize */
	a2fbd_Deinitialize	/* Deinitialize */
};
