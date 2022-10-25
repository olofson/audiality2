/*
 * env.c - Audiality 2 envelope generator unit
 *
 * Copyright 2016, 2022 David Olofson <david@olofson.net>
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
#include <stdlib.h>
#include "env.h"

#define	A2ENV_LUTSIZE	64


/* Process-wide lookup tables */
typedef struct A2ENV_lut
{
	float		lut[A2ENV_LUTSIZE + 2];
} A2ENV_lut;

typedef enum A2ENV_luts
{
	A2ENVLUT_SPLINE = 0,
	A2ENVLUT_EXP1,
	A2ENVLUT_EXP2,
	A2ENVLUT_EXP3,
	A2ENVLUT_EXP4,
	A2ENVLUT_EXP5,
	A2ENVLUT_EXP6,
	A2ENVLUT_EXP7,
	A2ENVLUT__COUNT
} A2ENV_luts;

static int lutsrc = 0;
static A2ENV_lut *luts;


typedef enum A2ENV_cins
{
	A2ENVCI_TARGET = 0,
	A2ENVCI_MODE,
	A2ENVCI_DOWN,
	A2ENVCI_TIME
} A2ENV_cins;

typedef enum A2ENV_couts
{
	A2ENVCO_OUT = 0
} A2ENV_couts;

typedef enum A2ENV_rampmodes
{
	A2ENVRM_IEXP7 =		-8,
	A2ENVRM_IEXP6 =		-7,
	A2ENVRM_IEXP5 =		-6,
	A2ENVRM_IEXP4 =		-5,
	A2ENVRM_IEXP3 =		-4,
	A2ENVRM_IEXP2 =		-3,
	A2ENVRM_IEXP1 =		-2,
	A2ENVRM_SPLINE =	-1,
	A2ENVRM_LINK =		0,
	A2ENVRM_LINEAR =	1,
	A2ENVRM_EXP1 =		2,
	A2ENVRM_EXP2 =		3,
	A2ENVRM_EXP3 =		4,
	A2ENVRM_EXP4 =		5,
	A2ENVRM_EXP5 =		6,
	A2ENVRM_EXP6 =		7,
	A2ENVRM_EXP7 =		8
} A2ENV_rampmodes;

typedef struct A2_env
{
	A2_unit		header;
	A2ENV_lut	*lut;
	A2_ramper	ramper;
	float		msdur;		/* One ms in sample frames */

	/* Output transform for non-linear modes */
	float		scale;
	float		offset;
	float		out;
} A2_env;


static inline A2_env *env_cast(A2_unit *u)
{
	return (A2_env *)u;
}


/* Callback for "off", step, linear ramping, and "output not connected" */
static void env_ProcessOff(A2_unit *u, unsigned offset, unsigned frames)
{
}


/* Callback for running segments using LUTs */
static void env_ProcessLUT(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_env *env = env_cast(u);
	A2_cport *co = &u->coutputs[A2ENVCO_OUT];
	A2_ramper *r = &env->ramper;
	float *t = env->lut->lut;
	uint32_t i;
	float f;
	a2_PrepareRamper(r, frames);
	a2_RunRamper(r, frames);
	i = r->value * A2ENV_LUTSIZE;
	f = r->value - i * A2ENV_LUTSIZE;
	env->out = (f * t[i + 1] + (1.0f - f) * t[i]) * env->scale + env->offset;
	co->write(co->unit, env->out, offset, frames << 8);
	if(!r->delta)
		u->Process = env_ProcessOff;	/* Done! */
}


static void env_Target(A2_unit *u, float v, unsigned start, unsigned dur)
{
	A2_env *env = env_cast(u);
	float *ci = u->registers;
	A2_cport *co = &u->coutputs[A2ENVCO_OUT];
	A2_ramper *r = &env->ramper;
	float rstart, rend;
	int mode;
	if(!co->write)
		return;

	/* Ramp duration override */
	if(ci[A2ENVCI_TIME])
		dur = ci[A2ENVCI_TIME] * env->msdur * 256.0f;

	if(dur >= 256 - start)
	{
		/* Select direction */
		mode = ci[A2ENVCI_DOWN];
		if((v >= env->out) || (mode == A2ENVRM_LINK))
			mode = ci[A2ENVCI_MODE];
	}
	else
	{
		/* Shortcut for zero duration ramps! */
		mode = A2ENVRM_LINEAR;
	}

	switch(mode)
	{
	  case A2ENVRM_LINK:
	  case A2ENVRM_LINEAR:
	  default:
		/*
		 * LINEAR modes. (LINK falls back to this on the 'mode'
		 * register, as there's nothing to link to!)
		 */
		env->out = v;	/* Set in case next segment needs it...! */
		co->write(co->unit, v, start, dur);
		u->Process = env_ProcessOff;
		return;
	  case A2ENVRM_SPLINE:
		env->lut = &luts[A2ENVLUT_SPLINE];
		mode = 1;	/* Forward! */
		break;
	  case A2ENVRM_EXP1:
	  case A2ENVRM_EXP2:
	  case A2ENVRM_EXP3:
	  case A2ENVRM_EXP4:
	  case A2ENVRM_EXP5:
	  case A2ENVRM_EXP6:
	  case A2ENVRM_EXP7:
		env->lut = &luts[A2ENVLUT_EXP1 + mode - A2ENVRM_EXP1];
		break;
	  case A2ENVRM_IEXP1:
	  case A2ENVRM_IEXP2:
	  case A2ENVRM_IEXP3:
	  case A2ENVRM_IEXP4:
	  case A2ENVRM_IEXP5:
	  case A2ENVRM_IEXP6:
	  case A2ENVRM_IEXP7:
		env->lut = &luts[A2ENVLUT_EXP1 - mode + A2ENVRM_IEXP1];
		break;
	}

	/* Select LUT traverse direction */
	if(mode >= 0)
	{
		/* Forward */
		rstart = 0.0f;
		rend = 1.0f;
		env->scale = v - env->out;
		env->offset = env->out;
	}
	else
	{
		/* Reverse */
		rstart = 1.0f;
		rend = 0.0f;
		env->scale = env->out - v;
		env->offset = env->out - env->scale;
	}

	/* Set up a unity ramp. (Translation done in env_Process*().) */
	r->value = rstart;
	a2_SetRamper(r, rend, start, dur);
	u->Process = env_ProcessLUT;
}


static A2_errors env_Initialize(A2_unit *u, A2_vmstate *vms,
		void *statedata, unsigned flags)
{
	A2_env *env = env_cast(u);
	A2_config *cfg = (A2_config *)statedata;
	float *ci = u->registers;
	env->msdur = cfg->samplerate * 0.001f;

	/* Internal state initialization */
	a2_InitRamper(&env->ramper, 0.0f);
	env->out = 0.0f;

	/* Initialize VM registers */
	ci[A2ENVCI_TARGET] = 0.0f;
	ci[A2ENVCI_MODE] = A2ENVRM_LINEAR;
	ci[A2ENVCI_DOWN] = A2ENVRM_LINK;
	ci[A2ENVCI_TIME] = 0.0f;

	/* Install Process callback */
	u->Process = env_ProcessOff;

	return A2_OK;
}


static A2_errors env_InitLUTs(void)
{
	int i, j;
	A2ENV_lut *t;
	if(!(luts = (A2ENV_lut *)malloc(sizeof(A2ENV_lut) * A2ENVLUT__COUNT)))
		return A2_OOMEMORY;

	/* Cosine spline table */
	t = &luts[A2ENVLUT_SPLINE];
	for(i = 0; i < A2ENV_LUTSIZE; ++i)
		t->lut[i] = 0.5f - 0.5f * cos(i * M_PI / (A2ENV_LUTSIZE - 1));

	/*
	 * "Tapered" exponential curves that are scaled and superimposed over
	 * linear functions, in order to produce curves that still "feel"
	 * exponential, but actually hit both 0 and 1, and have slightly
	 * reduced dynamic range, to avoid major parts of higher degree curves
	 * giving inaudible levels.
	 */
	for(j = A2ENVLUT_EXP1; j <= A2ENVLUT_EXP7; ++j)
	{
		const char deg[] = { 1, 2, 3, 4, 6, 9, 13 };
		float d = deg[j - A2ENVLUT_EXP1];
		double c = pow(0.1f, d);
		double rc = 0.002f + 0.1f * pow(0.8f, d);
		t = &luts[j];
		for(i = 0; i < A2ENV_LUTSIZE; ++i)
		{
			double x = 1.0f - (double)i / A2ENV_LUTSIZE;
			double r = (1.0f - x) * rc;
			t->lut[i] = pow(c, x) * (1.0f - r) + r - c * x;
		}
	}

	/* Set the 1.0 points at the end of each LUT! */
	for(i = 0; i < A2ENVLUT__COUNT; ++i) {
		luts[i].lut[A2ENV_LUTSIZE] = 1.0f;
		luts[i].lut[A2ENV_LUTSIZE + 1] = 1.0f;
	}

	return A2_OK;
}


static A2_errors env_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	if(!lutsrc)
	{
		A2_errors res = env_InitLUTs();
		if(res)
			return res;
	}
	++lutsrc;
	return A2_OK;
}


static void env_CloseState(void *statedata)
{
	if(!--lutsrc)
	{
		free(luts);
		luts = NULL;
	}
}


static const A2_crdesc cregs[] =
{
	{ "target",	env_Target		},	/* A2ENVCI_TARGET */
	{ "mode",	NULL			},	/* A2ENVCI_MODE */
	{ "down",	NULL			},	/* A2ENVCI_DOWN */
	{ "time",	NULL			},	/* A2ENVCI_TIME */
	{ NULL,	NULL				}
};

static const A2_codesc couts[] =
{
	{ "out"					},	/* A2ENVCO_OUT */
	{ NULL					}
};

static const A2_constdesc constants[] =
{
	{ "IEXP7",	A2ENVRM_IEXP7		},
	{ "IEXP6",	A2ENVRM_IEXP6		},
	{ "IEXP5",	A2ENVRM_IEXP5		},
	{ "IEXP4",	A2ENVRM_IEXP4		},
	{ "IEXP3",	A2ENVRM_IEXP3		},
	{ "IEXP2",	A2ENVRM_IEXP2		},
	{ "IEXP1",	A2ENVRM_IEXP1		},
	{ "SPLINE",	A2ENVRM_SPLINE		},
	{ "LINK",	A2ENVRM_LINK		},
	{ "LINEAR",	A2ENVRM_LINEAR		},
	{ "EXP1",	A2ENVRM_EXP1		},
	{ "EXP2",	A2ENVRM_EXP2		},
	{ "EXP3",	A2ENVRM_EXP3		},
	{ "EXP4",	A2ENVRM_EXP4		},
	{ "EXP5",	A2ENVRM_EXP5		},
	{ "EXP6",	A2ENVRM_EXP6		},
	{ "EXP7",	A2ENVRM_EXP7		},
	{ NULL,	0				}
};

const A2_unitdesc a2_env_unitdesc =
{
	"env",			/* name */

	0,			/* flags */

	cregs,			/* registers */
	couts,			/* coutputs */

	constants,		/* constants */

	0, 0,			/* [min,max]inputs */
	0, 0,			/* [min,max]outputs */

	sizeof(A2_env),		/* instancesize */
	env_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	env_OpenState,		/* OpenState */
	env_CloseState		/* CloseState */
};
