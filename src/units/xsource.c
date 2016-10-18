/*
 * xsource.c - Audiality 2 External Source unit
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

static inline void xsrc_clear(int32_t *out, unsigned offset, unsigned frames)
{
	memset(out + offset, 0, frames * sizeof(int32_t));
}


static inline void xsrc_add(int32_t *in, int32_t *out, unsigned offset,
		unsigned frames)
{
	int s;
	for(s = offset; s < offset + frames; ++s)
		out[s] += in[s];
}


static inline void xsrc_process(A2_unit *u, unsigned o, unsigned f, int add)
{
	int i;
	A2_errors res;
	A2_xinsert *xi = a2_xinsert_cast(u);
	A2_xinsert_client *xic;
	int32_t bufs[A2_MAXCHANNELS][A2_MAXFRAG];
	int32_t *bufp[A2_MAXCHANNELS];
	for(i = 0; i < u->noutputs; ++i)
		if(add)
			bufp[i] = bufs[i] + o;
		else
			bufp[i] = u->outputs[i] + o;
	if(!add)
		for(i = 0; i < u->noutputs; ++i)
			xsrc_clear(u->outputs[i], o, f);
	for(xic = xi->clients; xic; xic = xic->next)
	{
		if((res = xic->callback(bufp, u->noutputs, f, xic->userdata)))
			a2r_Error(xi->state, res, "xsource client callback");
		for(i = 0; i < u->noutputs; ++i)
			xsrc_add(bufs[i], u->outputs[i], o, f);
	}
}


/* Two or more clients, overwrite mode. */
static void xsrc_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	xsrc_process(u, offset, frames, 0);
}


/* One or more clients, adding mode. */
static void xsrc_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	xsrc_process(u, offset, frames, 1);
}


/* Single client, overwrite mode - no intermediate buffers needed! */
static void xsrc_ProcessSingle(A2_unit *u, unsigned offset, unsigned frames)
{
	int i;
	A2_errors res;
	A2_xinsert *xi = a2_xinsert_cast(u);
	A2_xinsert_client *xic = xi->clients;
	int32_t *bufp[A2_MAXCHANNELS];
	for(i = 0; i < u->noutputs; ++i)
		bufp[i] = u->outputs[i] + offset;
	if((res = xic->callback(bufp, u->noutputs, frames, xic->userdata)))
		a2r_Error(xi->state, res, "xsource client callback");
}


static void xsrc_ProcessNil(A2_unit *u, unsigned offset,
		unsigned frames)
{
	int i;
	for(i = 0; i < u->noutputs; ++i)
		xsrc_clear(u->outputs[i], offset, frames);
}


static void xsrc_ProcessNilAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	/* (No inputs and no clients...) */
}


/* Install the appropriate Process callback */
static void xsrc_SetProcess(A2_unit *u)
{
	A2_xinsert *xi = a2_xinsert_cast(u);
	if(xi->clients)
	{
		if(xi->flags & A2_PROCADD)
			u->Process = xsrc_ProcessAdd;
		else if(xi->clients->next)
			u->Process = xsrc_Process;
		else
			u->Process = xsrc_ProcessSingle;
	}
	else
	{
		if(xi->flags & A2_PROCADD)
			u->Process = xsrc_ProcessNilAdd;
		else
			u->Process = xsrc_ProcessNil;
	}
}


static A2_errors xsrc_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	A2_xinsert *xi = a2_xinsert_cast(u);
	A2_voice *v = a2_voice_from_vms(vms);

	/* Initialize private fields */
	xi->state = (A2_state *)statedata;
	xi->flags = flags;
	xi->clients = NULL;
	xi->voice = v->handle;
	xi->SetProcess = xsrc_SetProcess;

	xi->SetProcess(u);

	return A2_OK;
}


static void xsrc_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_xinsert *xi = a2_xinsert_cast(u);

	/* Remove all clients! */
	while(xi->clients)
		a2_XinsertRemoveClient(st, xi->clients);
}


static A2_errors xsrc_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg->state;
	return A2_OK;
}


const A2_unitdesc a2_xsource_unitdesc =
{
	"xsource",			/* name */

	A2_XINSERT,			/* flags */

	NULL,				/* registers */
	NULL,				/* control outputs */

	NULL,				/* constants */

	0, 0,				/* [min,max]inputs */
	1, A2_MAXCHANNELS,		/* [min,max]outputs */

	sizeof(A2_xinsert),		/* instancesize */
	xsrc_Initialize,		/* Initialize */
	xsrc_Deinitialize,		/* Deinitialize */

	xsrc_OpenState,			/* OpenState */
	NULL				/* CloseState */
};
