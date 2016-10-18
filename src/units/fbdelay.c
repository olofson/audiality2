/*
 * fbdelay.c - Audiality 2 feedback delay unit
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

#include <stdlib.h>
#include "fbdelay.h"

/*FIXME: Select buffer size based on sample rate and maximum delay allowed! */
#define A2FBD_BUFSIZE	131072

/* Control register frame enumeration */
typedef enum A2FBD_cregisters
{
	A2FBDR_FBDELAY = 0,
	A2FBDR_LDELAY,
	A2FBDR_RDELAY,
	A2FBDR_DRYGAIN,
	A2FBDR_FBGAIN,
	A2FBDR_LGAIN,
	A2FBDR_RGAIN
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


static inline A2_fbdelay *fbdelay_cast(A2_unit *u)
{
	return (A2_fbdelay *)u;
}


#define	WI(x)	((fbd->bufpos - (x)) & (A2FBD_BUFSIZE - 1))
static inline void fbdelay_process(A2_unit *u, unsigned offset,
		unsigned frames, int add, int stereoin, int stereoout)
{
	A2_fbdelay *fbd = fbdelay_cast(u);
	unsigned s, end = offset + frames;
	int32_t	*b0 = fbd->lbuf;
	int32_t	*b1 = fbd->rbuf;
	int32_t *in0 = u->inputs[0];
	int32_t *in1 = u->inputs[stereoin ? 1 : 0];
	int32_t *out0 = u->outputs[0];
	int32_t *out1;
	if(stereoout)
		out1 = u->outputs[1];
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
			if(stereoout)
			{
				out0[s] += o0;
				out1[s] += o1;
			}
			else
				out0[s] += (o0 + o1) >> 1;
		}
		else
		{
			if(stereoout)
			{
				out0[s] = o0;
				out1[s] = o1;
			}
			else
				out0[s] = (o0 + o1) >> 1;
		}
		++fbd->bufpos;
	}
}
#undef	WI

static void fbdelay_Process22Add(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 1, 1, 1);
}

static void fbdelay_Process22(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 0, 1, 1);
}

static void fbdelay_Process12Add(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 1, 0, 1);
}

static void fbdelay_Process12(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 0, 0, 1);
}

static void fbdelay_Process21Add(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 1, 1, 0);
}

static void fbdelay_Process21(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 0, 1, 0);
}

static void fbdelay_Process11Add(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 1, 0, 0);
}

static void fbdelay_Process11(A2_unit *u, unsigned offset, unsigned frames)
{
	fbdelay_process(u, offset, frames, 0, 0, 0);
}


static A2_errors fbdelay_Initialize(A2_unit *u, A2_vmstate *vms,
		void *statedata, unsigned flags)
{
	A2_config *cfg = (A2_config *)statedata;
	A2_fbdelay *fbd = fbdelay_cast(u);
	int *ur = u->registers;

	fbd->samplerate = cfg->samplerate;

/*FIXME: Select buffer size based on sample rate and maximum delay allowed! */
	fbd->lbuf = calloc(A2FBD_BUFSIZE, sizeof(int32_t));
	fbd->rbuf = calloc(A2FBD_BUFSIZE, sizeof(int32_t));
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
		switch(((u->ninputs - 1) << 1) + (u->noutputs - 1))
		{
		  case 0: u->Process = fbdelay_Process11Add; break;
		  case 1: u->Process = fbdelay_Process12Add; break;
		  case 2: u->Process = fbdelay_Process21Add; break;
		  case 3: u->Process = fbdelay_Process22Add; break;
		}
	else
		switch(((u->ninputs - 1) << 1) + (u->noutputs - 1))
		{
		  case 0: u->Process = fbdelay_Process11; break;
		  case 1: u->Process = fbdelay_Process12; break;
		  case 2: u->Process = fbdelay_Process21; break;
		  case 3: u->Process = fbdelay_Process22; break;
		}

	return A2_OK;
}

static void fbdelay_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_fbdelay *fbd = fbdelay_cast(u);
/*FIXME: Use realtime safe memory manager! */
	free(fbd->lbuf);
	free(fbd->rbuf);
}


static void fbdelay_FBDelay(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_fbdelay *fbd = fbdelay_cast(u);
	fbd->fbdelay = (int64_t)v * fbd->samplerate / 65536000;
}

static void fbdelay_LDelay(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_fbdelay *fbd = fbdelay_cast(u);
	fbd->ldelay = (int64_t)v * fbd->samplerate / 65536000;
}

static void fbdelay_RDelay(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_fbdelay *fbd = fbdelay_cast(u);
	fbd->rdelay = (int64_t)v * fbd->samplerate / 65536000;
}

static void fbdelay_DryGain(A2_unit *u, int v, unsigned start, unsigned dur)
{
	fbdelay_cast(u)->drygain = v;
}

static void fbdelay_FBGain(A2_unit *u, int v, unsigned start, unsigned dur)
{
	fbdelay_cast(u)->fbgain = v;
}

static void fbdelay_LGain(A2_unit *u, int v, unsigned start, unsigned dur)
{
	fbdelay_cast(u)->lgain = v;
}

static void fbdelay_RGain(A2_unit *u, int v, unsigned start, unsigned dur)
{
	fbdelay_cast(u)->rgain = v;
}


static A2_errors fbdelay_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	return A2_OK;
}


static const A2_crdesc regs[] =
{
	{ "fbdelay",	fbdelay_FBDelay		},	/* A2FBDR_FBDELAY */
	{ "ldelay",	fbdelay_LDelay		},	/* A2FBDR_LDELAY */
	{ "rdelay",	fbdelay_RDelay		},	/* A2FBDR_RDELAY */
	{ "drygain",	fbdelay_DryGain		},	/* A2FBDR_DRYGAIN */
	{ "fbgain",	fbdelay_FBGain		},	/* A2FBDR_FBGAIN */
	{ "lgain",	fbdelay_LGain		},	/* A2FBDR_LGAIN */
	{ "rgain",	fbdelay_RGain		},	/* A2FBDR_RGAIN */
	{ NULL,	NULL				}
};

const A2_unitdesc a2_fbdelay_unitdesc =
{
	"fbdelay",		/* name */

	0,			/* flags */

	regs,			/* registers */
	NULL,			/* constants */

	1, 2,			/* [min,max]inputs */
	1, 2,			/* [min,max]outputs */

	sizeof(A2_fbdelay),	/* instancesize */
	fbdelay_Initialize,	/* Initialize */
	fbdelay_Deinitialize,	/* Deinitialize */

	fbdelay_OpenState,	/* OpenState */
	NULL			/* CloseState */
};
