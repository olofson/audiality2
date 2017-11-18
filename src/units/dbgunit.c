/*
 * dbgunit.c - Audiality 2 debug unit
 *
 * Copyright 2012-2014, 2016-2017 David Olofson <david@olofson.net>
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
	A2_interface *i = &du->state->interfaces->interface;
	int min = 0x7fffffff;
	int max = 0x80000000;
	for(int ch = 0; ch < u->noutputs; ++ch)
	{
		for(int s = 0; s < frames; ++s)
		{
			int v = u->inputs[ch][offset + s];
			if(v < min)
				min = v;
			if(v > max)
				max = v;
			u->outputs[ch][offset + s] += v;
		}
	}
	A2_LOG_MSG(i, "dbgunit[%u]: ProcessAdd() %p o: %u, f: %u, peak:%d/%d",
			du->instance, u->outputs[0], offset, frames, min, max);
}


static void dbgunit_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	A2_interface *i = &du->state->interfaces->interface;
	int min = 0x7fffffff;
	int max = 0x80000000;
	for(int ch = 0; ch < u->noutputs; ++ch)
		for(int s = 0; s < frames; ++s)
		{
			int v = u->inputs[ch][offset + s];
			if(v < min)
				min = v;
			if(v > max)
				max = v;
			u->outputs[ch][offset + s] = v;
		}
	A2_LOG_MSG(i, "dbgunit[%u]: Process() %p o: %u, f: %u, peak:%d/%d",
			du->instance, u->outputs[0], offset, frames, min, max);
}


static void dbgunit_ProcessAddNI(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	A2_interface *i = &du->state->interfaces->interface;
	A2_LOG_MSG(i, "dbgunit[%u]: ProcessAddNI() o: %u, f: %u", du->instance,
			offset, frames);
}


static void dbgunit_ProcessNI(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_dbgunit *du = dbgunit_cast(u);
	A2_interface *i = &du->state->interfaces->interface;
	A2_LOG_MSG(i, "dbgunit[%u]: ProcessNI() o: %u, f: %u", du->instance,
			offset, frames);
	for(int ch = 0; ch < u->noutputs; ++ch)
		memset(u->outputs[ch] + offset, 0, frames * sizeof(int));
}


static A2_errors dbgunit_Initialize(A2_unit *u, A2_vmstate *vms,
		void *statedata, unsigned flags)
{
	A2_dbgunit *du = dbgunit_cast(u);
	A2_interface *i = &du->state->interfaces->interface;
	if(u->ninputs && (u->ninputs != u->noutputs))
		return A2_IODONTMATCH;
	du->instance = ++dbgunit_instance_count;
	du->state = (A2_state *)statedata;
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
	A2_LOG_MSG(i, "dbgunit[%u]: Initialize(), %s mode", du->instance,
			flags & A2_PROCADD ? "adding" : "replacing");
	return A2_OK;
}


static void dbgunit_Deinitialize(A2_unit *u)
{
	A2_dbgunit *du = dbgunit_cast(u);
	A2_interface *i = &du->state->interfaces->interface;
	A2_LOG_MSG(i, "dbgunit[%u]: Deinitialize()", du->instance);
}


static A2_errors dbgunit_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = ((A2_interface_i *)cfg->interface)->state;
	return A2_OK;
}


const A2_unitdesc a2_dbgunit_unitdesc =
{
	"dbgunit",		/* name */

	/*
	 * Can't use A2_MATCHIO, because this unit also works with no inputs at
	 * all. But, does it ever make sense for this unit to have ninputs !=
	 * noutputs when there are inputs...?
	 */
	0,			/* flags */

	NULL,			/* registers */
	NULL,			/* coutputs */

	NULL,			/* constants */

	0, A2_MAXCHANNELS,	/* [min,max]inputs */
	0, A2_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_dbgunit),	/* instancesize */
	dbgunit_Initialize,	/* Initialize */
	dbgunit_Deinitialize,	/* Deinitialize */

	dbgunit_OpenState,	/* OpenState */
	NULL			/* CloseState */
};
