/*
 * dc.c - Audiality 2 ramping DC generator unit
 *
 * Copyright 2016 David Olofson <david@olofson.net>
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

#include "dc.h"

#define	A2DC_MAXOUTPUTS	2

typedef enum A2DC_cregisters
{
	A2DCR_VALUE = 0,
	A2DCR_MODE
} A2DC_cregisters;

typedef enum A2DC_rampmodes
{
	A2DCRM_STEP = 0,
	A2DCRM_LINEAR,
#if 0
	A2DCRM_QUADRATIC,
	A2DCRM_CUBIC
#endif
} A2DC_rampmodes;

typedef struct A2_dc
{
	A2_unit		header;
	A2_ramper	value;		/* Value */
	A2DC_rampmodes	mode;		/* Output ramping mode */
} A2_dc;


static inline A2_dc *dc_cast(A2_unit *u)
{
	return (A2_dc *)u;
}


static inline void dc_process(A2_unit *u, unsigned offset, unsigned frames,
		int outputs, int add)
{
	A2_dc *dc = dc_cast(u);
	A2_ramper *v = &dc->value;
	unsigned s, o, end = offset + frames;
	int32_t **out = u->outputs;
	switch(dc->mode)
	{
	  case A2DCRM_STEP:
	  {
		s = offset;

		/* Fill with v->value until switch point */
		if(v->timer >= 256)
		{
			unsigned e2;
			if(v->timer >> 8 >= frames)
			{
				e2 = end;
				v->timer -= frames << 8;
			}
			else
			{
				e2 = s + (v->timer >> 8);
				v->timer &= 0xff;
			}
			for( ; s < e2; ++s)
				for(o = 0; o < outputs; ++o)
					if(add)
						out[o][s] += v->value;
					else
						out[o][s] = v->value;
		}

		/* One "transient" sample */
		if((v->timer < 256) && (s < end))
		{
			/* TODO: minBLEP or similar */
			int tv = ((v->value >> 4) * v->timer +
					(v->target >> 4) * (256 - v->timer)
					) >> 4;
			for(o = 0; o < outputs; ++o)
				if(add)
					out[o][s] += tv;
				else
					out[o][s] = tv;
			++s;
			v->timer = 0;
			v->value = v->target;	/* Switch! */
		}

		/* Fill with v->target from switch point to infinity */
		for( ; s < end; ++s)
			for(o = 0; o < outputs; ++o)
				if(add)
					out[o][s] += v->target;
				else
					out[o][s] = v->target;
		break;
	  }
	  case A2DCRM_LINEAR:
		/* TODO: "Transient" sample between ramps */
		/* TODO: minBLEP or similar */
		a2_PrepareRamper(v, frames);
		for(s = offset; s < end; ++s)
		{
			for(o = 0; o < outputs; ++o)
				if(add)
					out[o][s] += v->value;
				else
					out[o][s] = v->value;
			a2_RunRamper(v, 1);
		}
		break;
#if 0
	  case A2DCRM_QUADRATIC:
		break;
	  case A2DCRM_CUBIC:
		break;
#endif
	}
}


static void dc_Process1(A2_unit *u, unsigned offset, unsigned frames)
{
	dc_process(u, offset, frames, 1, 0);
}


static void dc_Process2(A2_unit *u, unsigned offset, unsigned frames)
{
	dc_process(u, offset, frames, 2, 0);
}


static void dc_Process1Add(A2_unit *u, unsigned offset, unsigned frames)
{
	dc_process(u, offset, frames, 1, 1);
}


static void dc_Process2Add(A2_unit *u, unsigned offset, unsigned frames)
{
	dc_process(u, offset, frames, 2, 1);
}


static A2_errors dc_Initialize(A2_unit *u, A2_vmstate *vms,
		void *statedata, unsigned flags)
{
	A2_dc *dc = dc_cast(u);
	int *ur = u->registers;

	/* Internal state initialization */
	a2_InitRamper(&dc->value, 0);
	dc->mode = A2DCRM_LINEAR;

	/* Initialize VM registers */
	ur[A2DCR_VALUE] = 0;
	ur[A2DCR_MODE] = A2DCRM_LINEAR << 16;

	/* Install Process callback */
	if(flags & A2_PROCADD)
		switch(u->noutputs)
		{
		  case 1: u->Process = dc_Process1Add; break;
		  case 2: u->Process = dc_Process2Add; break;
		}
	else
		switch(u->noutputs)
		{
		  case 1: u->Process = dc_Process1; break;
		  case 2: u->Process = dc_Process2; break;
		}

	return A2_OK;
}


static void dc_Value(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_dc *dc = dc_cast(u);
	switch(dc->mode)
	{
	  case A2DCRM_STEP:
		/* TODO: Handle value/target switch within current frame */
		dc->value.target = v << 8;
		dc->value.timer = (dur >> 1) - start;
		if(dc->value.timer <= 0)
		{
			dc->value.value = dc->value.target;
			dc->value.timer = 0;
		}
		break;
	  case A2DCRM_LINEAR:
		a2_SetRamper(&dc->value, v, start, dur);
		break;
#if 0
	  case A2DCRM_QUADRATIC:
		break;
	  case A2DCRM_CUBIC:
		break;
#endif
	}
}


static void dc_Mode(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_dc *dc = dc_cast(u);
	dc->mode = v >> 16;
	switch(dc->mode)
	{
	  default:
		dc->mode = A2DCRM_STEP;
		/* Fall-through! */
	  case A2DCRM_STEP:
		break;
	  case A2DCRM_LINEAR:
		break;
#if 0
	  case A2DCRM_QUADRATIC:
		break;
	  case A2DCRM_CUBIC:
		break;
#endif
	}
}


static const A2_crdesc regs[] =
{
	{ "value",	dc_Value		},	/* A2DCR_VALUE */
	{ "mode",	dc_Mode			},	/* A2DCR_MODE */
	{ NULL,	NULL				}
};

static const A2_constdesc constants[] =
{
	{ "STEP",	A2DCRM_STEP << 16	},
	{ "LINEAR",	A2DCRM_LINEAR << 16	},
#if 0
	{ "QUADRATIC",	A2DCRM_QUADRATIC << 16	},
	{ "CUBIC",	A2DCRM_CUBIC << 16	},
#endif
	{ NULL,	0				}
};

const A2_unitdesc a2_dc_unitdesc =
{
	"dc",			/* name */

	0,			/* flags */

	regs,			/* registers */
	constants,		/* constants */

	0, 0,			/* [min,max]inputs */
	1, A2DC_MAXOUTPUTS,	/* [min,max]outputs */

	sizeof(A2_dc),		/* instancesize */
	dc_Initialize,		/* Initialize */
	NULL,			/* Deinitialize */

	NULL,			/* OpenState */
	NULL			/* CloseState */
};
