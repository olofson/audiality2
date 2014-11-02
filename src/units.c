/*
 * units.c - Audiality 2 Voice Unit API
 *
 * Copyright 2010-2014 David Olofson <david@olofson.net>
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
#include <stdlib.h>
#include "internals.h"


static A2_mutex a2_unit_registry_mtx;


A2_errors a2_units_open(void)
{
	return a2_MutexOpen(&a2_unit_registry_mtx);
}


void a2_units_close(void)
{
	a2_MutexClose(&a2_unit_registry_mtx);
}


A2_errors a2_UnitOpenState(A2_state *st, unsigned uindex)
{
	const A2_unitdesc *ud = st->ss->units[uindex];
	if(!ud)
		return A2_NOTFOUND;	/* No unit! Failed when registered? */
	st->unitstate[uindex].statedata = NULL;
	st->unitstate[uindex].status = A2_OK;
	if(ud->OpenState)
	{
		a2_MutexLock(&a2_unit_registry_mtx);
		st->unitstate[uindex].status = ud->OpenState(st->config,
				&st->unitstate[uindex].statedata);
		a2_MutexUnlock(&a2_unit_registry_mtx);
	}
	return st->unitstate[uindex].status;
}


void a2_UnitCloseState(A2_state *st, unsigned uindex)
{
	const A2_unitdesc *ud = st->ss->units[uindex];
	if(!ud)
		return;		/* No unit! Failed when registered? */
	if(st->unitstate[uindex].status)
		return;		/* Failed in init for this state! */
	if(ud->CloseState)
	{
		a2_MutexLock(&a2_unit_registry_mtx);
		ud->CloseState(st->unitstate[uindex].statedata);
		a2_MutexUnlock(&a2_unit_registry_mtx);
	}
	st->unitstate[uindex].statedata = NULL;
	st->unitstate[uindex].status = A2_NOOBJECT;
}


A2_handle a2_RegisterUnit(A2_state *st, const A2_unitdesc *ud)
{
	int uindex = st->ss->nunits;
	A2_handle h;
	const A2_unitdesc **uds;
	A2_unitstate *uss;

	/*
	 * TODO: Allocate new statedata arrays here, call OpenState() as needed
	 *       for all states, and then tell them about the new unit in a
	 *       thread safe manner.
	 */
	if(st->parent || st->next)
	{
		fprintf(stderr, "Audiality 2: Tried to register unit '%s' "
				"on a state that shares data with other "
				"states, which is not yet supported! "
				"Please register all units before creating "
				"substates.\n", ud->name);
		return -A2_NOTIMPLEMENTED;
	}

	/* Some sanity checks, to detect broken units early */
	if(ud->flags & A2_MATCHIO)
	{
		if((ud->mininputs != ud->minoutputs) ||
				(ud->maxinputs != ud->maxoutputs))
		{
			fprintf(stderr, "Audiality 2: Unit '%s' has the "
					"A2_MATCHIO flag set, but mismatched "
					"mininputs/minoutputs fields!\n",
					ud->name);
			return -A2_IODONTMATCH;
		}
	}

	/* Add to sharedstate unitdesc table */
	uds = (const A2_unitdesc **)realloc(st->ss->units,
			sizeof(A2_unitdesc *) * (st->ss->nunits + 1));
	if(!uds)
		return -A2_OOMEMORY;
	++st->ss->nunits;
	st->ss->units = uds;
	st->ss->units[uindex] = ud;

	/* Add to master state unitstate table */
	uss = (A2_unitstate *)realloc(st->unitstate,
			sizeof(A2_unitstate) * st->ss->nunits);
	if(!uss)
	{
		st->ss->units[uindex] = NULL;
		return -A2_OOMEMORY;
	}
	st->unitstate = uss;

	if((h = a2_UnitOpenState(st, uindex)))
	{
		st->ss->units[uindex] = NULL;
		return -h;
	}

	/* Create a handle for it! */
	h = rchm_NewEx(&st->ss->hm, (char *)NULL + uindex, A2_TUNIT,
			A2_LOCKED, 1);
	if(h < 0)
		return h;

	DBG(printf("registered unit \"%s\", handle %d\n", ud->name, h);)
	return h;
}


const A2_unitdesc *a2_GetUnitDescriptor(A2_state *st, A2_handle handle)
{
	int ui = a2_GetUnit(st, handle);
	if(ui < 0)
		return NULL;
	return st->ss->units[ui];
}


static RCHM_errors a2_UnitDestructor(RCHM_handleinfo *hi, void *ti,
		RCHM_handle h)
{
	if(hi->userbits & A2_LOCKED)
		return RCHM_REFUSE;
	/* Unit descriptors and unit state data is handled elsewhere! */
	return RCHM_OK;
}


A2_errors a2_RegisterUnitTypes(A2_state *st)
{
	return a2_RegisterType(st, A2_TUNIT, "unit", a2_UnitDestructor, NULL);
}
