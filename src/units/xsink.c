/*
 * xsink.c - Audiality 2 External Sink unit
 *
 * Copyright 2014, 2016 David Olofson <david@olofson.net>
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

#include "xinsert.h"
#include "internals.h"
#include <stdlib.h>

static void xsink_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	int i;
	A2_errors res;
	A2_xinsert *xi = a2_xinsert_cast(u);
	A2_xinsert_client *xic = xi->clients;
	int32_t *bufp[A2_MAXCHANNELS];

	if(!xic)
		return;

	/* API doesn't support 'offset', so we need adjusted pointers! */
	for(i = 0; i < u->ninputs; ++i)
		bufp[i] = u->inputs[i] + offset;

	for( ; xic; xic = xic->next)
		if((res = xic->callback(bufp, u->ninputs, frames,
				xic->userdata)))
			a2r_Error(xi->state, res, "xsink client callback");
}


/* Install the appropriate Process callback */
static void xsink_SetProcess(A2_unit *u)
{
	/* (NOP, because input-only!) */
}


static A2_errors xsink_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	A2_xinsert *xi = a2_xinsert_cast(u);
	A2_voice *v = a2_voice_from_vms(vms);

	/* Initialize private fields */
	xi->state = (A2_state *)statedata;
	xi->flags = flags;
	xi->clients = NULL;
	xi->voice = v->handle;
	xi->SetProcess = xsink_SetProcess;
	u->Process = xsink_Process;

	return A2_OK;
}


static void xsink_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_xinsert *xi = a2_xinsert_cast(u);

	/* Remove all clients! */
	while(xi->clients)
		a2_XinsertRemoveClient(st, xi->clients);
}


static A2_errors xsink_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg->state;
	return A2_OK;
}


const A2_unitdesc a2_xsink_unitdesc =
{
	"xsink",			/* name */

	A2_XINSERT,			/* flags */

	NULL,				/* registers */
	NULL,				/* constants */

	1, A2_MAXCHANNELS,		/* [min,max]inputs */
	0, 0,				/* [min,max]outputs */

	sizeof(A2_xinsert),		/* instancesize */
	xsink_Initialize,		/* Initialize */
	xsink_Deinitialize,		/* Deinitialize */

	xsink_OpenState,		/* OpenState */
	NULL				/* CloseState */
};
