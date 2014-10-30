/*
 * fm.c - Audiality 2 1/2/3/4-operator FM oscillator units
 *
 * Copyright 2014 David Olofson <david@olofson.net>
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
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "fm.h"
#include "dsp.h"

#define	A2FM_MAX_OPERATORS	4

#define	A2FM_WAVEPERIOD_BITS	11
#define	A2FM_WAVEPERIOD		(1 << A2FM_WAVEPERIOD_BITS)
#define	A2FM_WAVEPERIOD_MASK	(A2FM_WAVEPERIOD - 1)
#define	A2FM_WAVEPAD		1

#ifdef A2_HIFI
#  define	A2FM1_OVERSAMPLE_BITS	1
#  define	A2FM2_OVERSAMPLE_BITS	2
#  define	A2FM3_OVERSAMPLE_BITS	3
#  define	A2FM4_OVERSAMPLE_BITS	4
#elif (defined A2_LOFI)
#  define	A2FM1_OVERSAMPLE_BITS	0
#  define	A2FM2_OVERSAMPLE_BITS	0
#  define	A2FM3_OVERSAMPLE_BITS	0
#  define	A2FM4_OVERSAMPLE_BITS	0
#else
#  define	A2FM1_OVERSAMPLE_BITS	0
#  define	A2FM2_OVERSAMPLE_BITS	1
#  define	A2FM3_OVERSAMPLE_BITS	2
#  define	A2FM4_OVERSAMPLE_BITS	2
#endif

/* Control register frame enumeration */
typedef enum A2FM_cregisters
{
	A2FMR_PHASE = 0,	/* Phase (sets phase on ALL operators!) */

	/* Operator 0 (master output) */
	A2FMR_PITCH0,		/* Master oscillator and base pitch */
	A2FMR_AMP0,		/* Master/output amplitude */
	A2FMR_FB0,		/* op0->op0 feedback modulation depth */

	/* Operator 1 */
	A2FMR_PITCH1,		/* Pitch, relative to op0 */
	A2FMR_AMP1,		/* op1->op0 modulation depth */
	A2FMR_FB1,		/* op1->op1 feedback modulation depth */

	/* Operator 2 */
	A2FMR_PITCH2,		/* Pitch, relative to op0 */
	A2FMR_AMP2,		/* op1->op0 modulation depth */
	A2FMR_FB2,		/* op2->op2 feedback modulation depth */

	/* Operator 3 */
	A2FMR_PITCH3,		/* Pitch, relative to op0 */
	A2FMR_AMP3,		/* op1->op0 modulation depth */
	A2FMR_FB3,		/* op3->op3 feedback modulation depth */
} A2FM_cregisters;
#define	A2FMR_OP_SIZE	(A2FMR_PITCH1 - A2FMR_PITCH0)

/* One oscillator */
typedef struct A2_fmosc
{
	A2_ramper	a;		/* Amplitude/depth */
	A2_ramper	fb;		/* Feedback modulation depth */
	unsigned	phase;		/* Phase (24:8 fixp, 1.0/sample) */
	unsigned	dphase;		/* Increment (8:24 fixp, 1.0/period) */
	int		last;		/* Previous raw sine value */
} A2_fmosc;

typedef struct A2_fm
{
	A2_unit		header;

	/* Needed for op0 pitch calculations */
	int		*transpose;
	int		*regs;
	float		onedivfs;	/* Actually, 8:24... */

	/* Oscillators/operators */
	unsigned	nops;
	A2_fmosc	op[A2FM_MAX_OPERATORS];
} A2_fm;


/* Process-wide sin() table */
static int sinerc = 0;
static int16_t *sine = NULL;


static inline int32_t fm_osc(A2_fmosc *o, int mod)
{
	int fb = (int64_t)(o->last) * o->fb.value >> 17;
	unsigned ph = (o->phase + mod + fb) >> (24 - 8 - A2FM_WAVEPERIOD_BITS);
#ifdef A2_LOFI
	o->last = sine[(ph >> 8) & A2FM_WAVEPERIOD_MASK];
#else
	/* We don't go beyond linear here, so "standard" == A2_HIFI. */
	o->last = a2_Lerp(sine, ph & ((A2FM_WAVEPERIOD << 8) - 1));
#endif
	return (int64_t)(o->last) * o->a.value >> 16;
}


static inline A2_fm *fm_cast(A2_unit *u)
{
	return (A2_fm *)u;
}


static inline int fm_f2dphase(A2_fm *fm, float f)
{
	return f * fm->onedivfs + 0.5f;
}


static inline void fm_process(A2_unit *u, unsigned offset, unsigned frames,
		int osbits, int operators, int add)
{
	A2_fm *fm = fm_cast(u);
	int i;
	unsigned s;
	unsigned oversample = 1 << osbits;
	unsigned end = offset + frames;
	int32_t *out = u->outputs[0];
	for(i = 0; i < operators; ++i)
	{
		a2_RamperPrepare(&fm->op[i].a, frames);
		a2_RamperPrepare(&fm->op[i].fb, frames);
	}
	for(s = offset; s < end; ++s)
	{
		int os;
		int vsum = 0;
		for(os = 0; os < oversample; ++os)
		{
			int v = 0;
			for(i = operators - 1; i >= 0; --i)
			{
				v = fm_osc(&fm->op[i], v);
				fm->op[i].phase += fm->op[i].dphase >> osbits;
			}
			vsum += v;
		}
		for(i = 0; i < operators; ++i)
		{
			a2_RamperRun(&fm->op[i].a, 1);
			a2_RamperRun(&fm->op[i].fb, 1);
			/* Fix the rounding error buildup! */
			fm->op[i].phase += fm->op[i].dphase & (oversample - 1);
		}
		if(add)
			out[s] += vsum >> osbits;
		else
			out[s] = vsum >> osbits;
	}
}

static void fm1_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM1_OVERSAMPLE_BITS, 1, 1);
}

static void fm1_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM1_OVERSAMPLE_BITS, 1, 0);
}

static void fm2_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM2_OVERSAMPLE_BITS, 2, 1);
}

static void fm2_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM2_OVERSAMPLE_BITS, 2, 0);
}

static void fm3_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM3_OVERSAMPLE_BITS, 3, 1);
}

static void fm3_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM3_OVERSAMPLE_BITS, 3, 0);
}

static void fm4_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM4_OVERSAMPLE_BITS, 4, 1);
}

static void fm4_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	fm_process(u, offset, frames, A2FM4_OVERSAMPLE_BITS, 4, 0);
}


/*
 *	ph	desired phase, [0, 1], (16):16 fixp
 *	sst	SubSample Time, [0, 1], (24):8 fixp
 */
static inline void fm_set_phase(A2_fm *fm, int ph, unsigned sst)
{
	int i;
	for(i = 0; i < fm->nops; ++i)
	{
		int ssph = ph + (sst * (fm->op[i].dphase >> 8) >> 8);
		fm->op[i].phase = ssph * A2FM_WAVEPERIOD >> 8;
	}
}


static A2_errors fm_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	int i;
	A2_config *cfg = (A2_config *)statedata;
	A2_fm *fm = fm_cast(u);
	fm->regs = u->registers;

	/* So... Don't rename these units, OK!? :-) */
	fm->nops = u->descriptor->name[2] - '0';

	/* Internal state initialization */
	fm->transpose = vms->r + R_TRANSPOSE;
	fm->onedivfs = 16777216.0f / cfg->samplerate;

	for(i = 0; i < fm->nops; ++i)
	{
		a2_RamperInit(&fm->op[i].a, 0);
		a2_RamperInit(&fm->op[i].fb, 0);
		fm->op[i].last = 0;
	}

	fm->op[0].dphase =  fm_f2dphase(fm, powf(2.0f,
			(*fm->transpose) * (1.0f / 65536.0f)) * A2_MIDDLEC);
	for(i = 1; i < fm->nops; ++i)
		fm->op[i].dphase = fm->op[0].dphase;

	fm_set_phase(fm, 0, vms->waketime & 0xff);

	/* Initialize VM registers */
	fm->regs[A2FMR_PHASE] = 0;
	memset(fm->regs + A2FMR_PITCH0, 0,
			sizeof(int) * A2FMR_OP_SIZE * fm->nops);

	/* Install Process callback */
	if(flags & A2_PROCADD)
		switch(fm->nops)
		{
		  case 1:	u->Process = fm1_ProcessAdd;	break;
		  case 2:	u->Process = fm2_ProcessAdd;	break;
		  case 3:	u->Process = fm3_ProcessAdd;	break;
		  case 4:	u->Process = fm4_ProcessAdd;	break;
		}
	else
		switch(fm->nops)
		{
		  case 1:	u->Process = fm1_Process;	break;
		  case 2:	u->Process = fm2_Process;	break;
		  case 3:	u->Process = fm3_Process;	break;
		  case 4:	u->Process = fm4_Process;	break;
		}

	return A2_OK;
}


static void fm_Phase(A2_unit *u, int v, unsigned start, unsigned dur)
{
	fm_set_phase(fm_cast(u), v, start);
}


static void fm_Pitch(A2_unit *u, int v, unsigned start, unsigned dur)
{
	int i;
	A2_fm *fm = fm_cast(u);
	fm->op[0].dphase =  fm_f2dphase(fm, powf(2.0f,
			(v + *fm->transpose) * (1.0f / 65536.0f)) *
			A2_MIDDLEC);
	for(i = 1; i < fm->nops; ++i)
	{
		int rv = fm->regs[A2FMR_PITCH0 + i * A2FMR_OP_SIZE];
		fm->op[i].dphase = fm->op[0].dphase *
				powf(2.0f, rv * (1.0f / 65536.0f));
	}
}

static void fm_Amplitude(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[0].a, v, start, dur);
}

static void fm_Feedback(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[0].fb, v, start, dur);
}


static void fm_Pitch1(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_fm *fm = fm_cast(u);
	fm->op[1].dphase = fm->op[0].dphase *
			powf(2.0f, v * (1.0f / 65536.0f));
}

static void fm_Amplitude1(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[1].a, v, start, dur);
}

static void fm_Feedback1(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[1].fb, v, start, dur);
}


static void fm_Pitch2(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_fm *fm = fm_cast(u);
	fm->op[2].dphase = fm->op[0].dphase *
			powf(2.0f, v * (1.0f / 65536.0f));
}

static void fm_Amplitude2(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[2].a, v, start, dur);
}

static void fm_Feedback2(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[2].fb, v, start, dur);
}


static void fm_Pitch3(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_fm *fm = fm_cast(u);
	fm->op[3].dphase = fm->op[0].dphase *
			powf(2.0f, v * (1.0f / 65536.0f));
}

static void fm_Amplitude3(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[3].a, v, start, dur);
}

static void fm_Feedback3(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&fm_cast(u)->op[3].fb, v, start, dur);
}


static A2_errors fm_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	if(!sinerc)
	{
		int s;
		int len = A2FM_WAVEPERIOD + A2FM_WAVEPAD;
		if(!(sine = malloc(sizeof(int16_t) * len)))
			return A2_OOMEMORY;
		for(s = 0; s < len; ++s)
			sine[s] = sin(s * 2.0f * M_PI / A2FM_WAVEPERIOD) *
					32767.0f;
	}
	++sinerc;
	return A2_OK;
}

static void fm_CloseState(void *statedata)
{
	if(--sinerc)
	{
		free(sine);
		sine = NULL;
	}
}


static const A2_crdesc fm1_regs[] =
{
	/* Common controls */
	{ "phase",	fm_Phase		},	/* A2FMR_PHASE */

	/* Master Oscillator */
	{ "p",		fm_Pitch		},	/* A2FMR_PITCH0 */
	{ "a",		fm_Amplitude		},	/* A2FMR_AMP0 */
	{ "fb",		fm_Feedback		},	/* A2FMR_FB0 */

	{ NULL,	NULL				}
};

const A2_unitdesc a2_fm1_unitdesc =
{
	"fm1",			/* name */

	0,			/* flags */

	fm1_regs,		/* registers */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_fm),		/* instancesize */
	fm_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	fm_OpenState,		/* OpenState */
	fm_CloseState		/* CloseState */
};


static const A2_crdesc fm2_regs[] =
{
	/* Common controls */
	{ "phase",	fm_Phase		},	/* A2FMR_PHASE */

	/* Master Oscillator */
	{ "p",		fm_Pitch		},	/* A2FMR_PITCH0 */
	{ "a",		fm_Amplitude		},	/* A2FMR_AMP0 */
	{ "fb",		fm_Feedback		},	/* A2FMR_FB0 */

	/* Operator 1 */
	{ "p1",		fm_Pitch1		},	/* A2FMR_PITCH1 */
	{ "a1",		fm_Amplitude1		},	/* A2FMR_AMP1 */
	{ "fb1",	fm_Feedback1		},	/* A2FMR_FB1 */

	{ NULL,	NULL				}
};

const A2_unitdesc a2_fm2_unitdesc =
{
	"fm2",			/* name */

	0,			/* flags */

	fm2_regs,		/* registers */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_fm),		/* instancesize */
	fm_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	fm_OpenState,		/* OpenState */
	fm_CloseState		/* CloseState */
};


static const A2_crdesc fm3_regs[] =
{
	/* Common controls */
	{ "phase",	fm_Phase		},	/* A2FMR_PHASE */

	/* Master Oscillator */
	{ "p",		fm_Pitch		},	/* A2FMR_PITCH0 */
	{ "a",		fm_Amplitude		},	/* A2FMR_AMP0 */
	{ "fb",		fm_Feedback		},	/* A2FMR_FB0 */

	/* Operator 1 */
	{ "p1",		fm_Pitch1		},	/* A2FMR_PITCH1 */
	{ "a1",		fm_Amplitude1		},	/* A2FMR_AMP1 */
	{ "fb1",	fm_Feedback1		},	/* A2FMR_FB1 */

	/* Operator 2 */
	{ "p2",		fm_Pitch2		},	/* A2FMR_PITCH2 */
	{ "a2",		fm_Amplitude2		},	/* A2FMR_AMP2 */
	{ "fb2",	fm_Feedback2		},	/* A2FMR_FB2 */

	{ NULL,	NULL				}
};

const A2_unitdesc a2_fm3_unitdesc =
{
	"fm3",			/* name */

	0,			/* flags */

	fm3_regs,		/* registers */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_fm),		/* instancesize */
	fm_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	fm_OpenState,		/* OpenState */
	fm_CloseState		/* CloseState */
};


static const A2_crdesc fm4_regs[] =
{
	/* Common controls */
	{ "phase",	fm_Phase		},	/* A2FMR_PHASE */

	/* Master Oscillator */
	{ "p",		fm_Pitch		},	/* A2FMR_PITCH0 */
	{ "a",		fm_Amplitude		},	/* A2FMR_AMP0 */
	{ "fb",		fm_Feedback		},	/* A2FMR_FB0 */

	/* Operator 1 */
	{ "p1",		fm_Pitch1		},	/* A2FMR_PITCH1 */
	{ "a1",		fm_Amplitude1		},	/* A2FMR_AMP1 */
	{ "fb1",	fm_Feedback1		},	/* A2FMR_FB1 */

	/* Operator 2 */
	{ "p2",		fm_Pitch2		},	/* A2FMR_PITCH2 */
	{ "a2",		fm_Amplitude2		},	/* A2FMR_AMP2 */
	{ "fb2",	fm_Feedback2		},	/* A2FMR_FB2 */

	/* Operator 3 */
	{ "p3",		fm_Pitch3		},	/* A2FMR_PITCH3 */
	{ "a3",		fm_Amplitude3		},	/* A2FMR_AMP3 */
	{ "fb3",	fm_Feedback3		},	/* A2FMR_FB3 */

	{ NULL,	NULL				}
};

const A2_unitdesc a2_fm4_unitdesc =
{
	"fm4",			/* name */

	0,			/* flags */

	fm4_regs,		/* registers */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_fm),		/* instancesize */
	fm_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	fm_OpenState,		/* OpenState */
	fm_CloseState		/* CloseState */
};
