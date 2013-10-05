/*
 * dbgunit.c - Audiality 2 debug unit
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

#include <stdio.h>
#include "dbgunit.h"
#include "internals.h"


typedef struct A2_dbgunit
{
	A2_unit		header;
	A2_state	*state;
	A2_voice	*voice;
	unsigned	instance;
} A2_dbgunit;


static inline A2_dbgunit *dbgunit_cast(A2_unit *u)
{
	return (A2_dbgunit *)u;
}


volatile static unsigned dbgunit_instance_count = 0;


static void dbgunit_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	int i, s;
	int min = 0x7fffffff;
	int max = 0x80000000;
	for(i = 0; i < u->noutputs; ++i)
		for(s = 0; s < frames; ++s)
		{
			int v = u->inputs[i][offset + s];
			if(v < min)
				min = v;
			if(v > max)
				max = v;
			u->outputs[i][offset + s] += v;
		}
	fprintf(stderr, "dbgunit[%u]: ProcessAdd() %p o: %u, f: %u, peak:%d/%d\n",
			du->instance, u->outputs[0], offset, frames, min, max);
}


static void dbgunit_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	int i, s;
	int min = 0x7fffffff;
	int max = 0x80000000;
	for(i = 0; i < u->noutputs; ++i)
		for(s = 0; s < frames; ++s)
		{
			int v = u->inputs[i][offset + s];
			if(v < min)
				min = v;
			if(v > max)
				max = v;
			u->outputs[i][offset + s] = v;
		}
	fprintf(stderr, "dbgunit[%u]: Process() %p o: %u, f: %u, peak:%d/%d\n",
			du->instance, u->outputs[0], offset, frames, min, max);
}


static void dbgunit_ProcessAddNI(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	fprintf(stderr, "dbgunit[%u]: ProcessAddNI() o: %u, f: %u\n",
			du->instance, offset, frames);
}


static void dbgunit_ProcessNI(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	int i;
	fprintf(stderr, "dbgunit[%u]: ProcessNI() o: %u, f: %u\n",
			du->instance, offset, frames);
	for(i = 0; i < u->noutputs; ++i)
		memset(u->outputs[i] + offset, 0, frames * sizeof(int));
}


static A2_errors dbgunit_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	A2_dbgunit *du = dbgunit_cast(u);
	if(u->ninputs && (u->ninputs != u->noutputs))
		return A2_IODONTMATCH;
	du->instance = ++dbgunit_instance_count;
	du->state = cfg->state;
	du->voice = a2_voice_from_vms(vms);
	if(u->ninputs)
	{
		if(flags & A2_PROCADD)
			u->Process = dbgunit_ProcessAdd;
		else
			u->Process = dbgunit_Process;
	}
	else
	{
		if(flags & A2_PROCADD)
			u->Process = dbgunit_ProcessAddNI;
		else
			u->Process = dbgunit_ProcessNI;
	}
	fprintf(stderr, "dbgunit[%u]: Initialize(), %s mode\n", du->instance,
			flags & A2_PROCADD ? "adding" : "replacing");
	return A2_OK;
}


static void dbgunit_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_dbgunit *du = dbgunit_cast(u);
	fprintf(stderr, "dbgunit[%u]: Deinitialize()\n", du->instance);
}


static const A2_crdesc regs[] =
{
	{ NULL, NULL }
};

const A2_unitdesc a2_dbgunit_unitdesc =
{
	"dbgunit",		/* name */

	regs,			/* registers */

	0, A2_MAXCHANNELS,	/* [min,max]inputs */
	0, A2_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_dbgunit),	/* instancesize */
	dbgunit_Initialize,	/* Initialize */
	dbgunit_Deinitialize	/* Deinitialize */
};
