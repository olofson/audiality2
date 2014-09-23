/*
 * xinsert.c - Audiality 2 External Insert unit
 *
 * Copyright 2012-2014 David Olofson <david@olofson.net>
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

static inline void xi_copy(int32_t *in, int32_t *out, unsigned offset,
		unsigned frames)
{
	int s;
	for(s = offset; s < offset + frames; ++s)
		out[s] = in[s];
}

static inline void xi_add(int32_t *in, int32_t *out, unsigned offset,
		unsigned frames)
{
	int s;
	for(s = offset; s < offset + frames; ++s)
		out[s] += in[s];
}


static inline void xi_run_callback(A2_unit *u, A2_xinsert_client *xic,
		unsigned offset, unsigned frames, int32_t **bufs)
{
	A2_errors res;
	A2_xinsert *xi = a2_xinsert_cast(u);
	int32_t *bufp[A2_MAXCHANNELS];
	int i;

	/* API doesn't support 'offset', so we need adjusted pointers! */
	for(i = 0; i < u->ninputs; ++i)
		bufp[i] = bufs[i] + offset;

	if((res = xic->callback(bufp, u->ninputs, frames, xic->userdata)))
		a2r_Error(xi->state, res, "xinsert client callback");
}


static inline void xi_process(A2_unit *u, unsigned o, unsigned f, int add)
{
	int i;
	A2_xinsert_client *xic;
	A2_xinsert *xi = a2_xinsert_cast(u);
	int32_t bufs[A2_MAXCHANNELS][A2_MAXFRAG];
	int32_t *bufp[A2_MAXCHANNELS];
	int32_t obufs[A2_MAXCHANNELS][A2_MAXFRAG];
	int32_t *obufp[A2_MAXCHANNELS];
	int has_inserts = 0;

	/*
	 * Set up pointers for processing and output buffers.
	 *
	 * Since we're not inherently inplace safe, in replace mode, we need
	 * to use intermediate output buffers for input/output pairs that
	 * share buffers! We also clear all output buffers (intermediate or
	 * external), since we're going to add the output from all clients into
	 * those buffers.
	 *
	 * In adding mode, inputs and outputs cannot share buffers, so we
	 * always use the actual output buffers directly.
	 */
	for(i = 0; i < u->ninputs; ++i)
	{
		bufp[i] = bufs[i];
		if(add || (u->inputs[i] != u->outputs[i]))
			obufp[i] = u->outputs[i];
		else
			obufp[i] = obufs[i];
		if(!add)
			memset(obufp[i], 0, sizeof(int32_t) * A2_MAXFRAG);
	}

	for(xic = xi->clients; xic; xic = xic->next)
	{
		if(!(xic->flags & A2_XI_WRITE))
		{
			/* READ-only client (assume no NOP clients...) */
			xi_run_callback(u, xic, o, f, u->inputs);
			continue;
		}

		if(xic->flags & A2_XI_READ)
		{
			/* INSERT (READ/WRITE): Copy the input first! */
			for(i = 0; i < u->ninputs; ++i)
				xi_copy(u->inputs[i], bufs[i], o, f);

			/* Disable built-in bypass! */
			has_inserts = 1;
		}

		/* Process! */
		xi_run_callback(u, xic, o, f, bufp);

		/* Mix the output into the "master" output buffers */
		for(i = 0; i < u->ninputs; ++i)
			xi_add(bufs[i], obufp[i], o, f);
	}

	/* If there are no insert (READ/WRITE) clients, enable bypass! */
	if(!has_inserts)
		for(i = 0; i < u->ninputs; ++i)
			xi_add(u->inputs[i], obufp[i], o, f);

	/* Replace: Write back any output buffers that were... buffered. :-) */
	if(!add)
		for(i = 0; i < u->ninputs; ++i)
			if(obufp[i] != u->outputs[i])
				xi_copy(obufp[i], u->outputs[i], o, f);
}

static void xi_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	xi_process(u, offset, frames, 0);
}

static void xi_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	xi_process(u, offset, frames, 1);
}


static void xi_ProcessBypass(A2_unit *u, unsigned offset,
		unsigned frames)
{
	int i;
	for(i = 0; i < u->ninputs; ++i)
		if(u->inputs[i] != u->outputs[i])
			xi_copy(u->inputs[i], u->outputs[i], offset, frames);
}


static void xi_ProcessBypassAdd(A2_unit *u, unsigned offset,
		unsigned frames)
{
	int i;
	for(i = 0; i < u->ninputs; ++i)
		xi_add(u->inputs[i], u->outputs[i], offset, frames);
}


/* Install the appropriate Process callback */
static void xi_set_process(A2_unit *u)
{
	A2_xinsert *xi = a2_xinsert_cast(u);
	if(xi->clients)
	{
		if(xi->flags & A2_PROCADD)
			u->Process = xi_ProcessAdd;
		else
			u->Process = xi_Process;
	}
	else
	{
		if(xi->flags & A2_PROCADD)
			u->Process = xi_ProcessBypassAdd;
		else
			u->Process = xi_ProcessBypass;
	}
}


static A2_errors xi_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	A2_xinsert *xi = a2_xinsert_cast(u);
	A2_voice *v = a2_voice_from_vms(vms);

	/* Initialize private fields */
	xi->state = cfg->state;
	xi->flags = flags;
	xi->clients = NULL;
	xi->voice = v->handle;

	xi_set_process(u);

	return A2_OK;
}


static void xi_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_xinsert *xi = a2_xinsert_cast(u);

	/* Remove all clients! */
	while(xi->clients)
		a2_XinsertRemoveClient(st, xi->clients);
}


static const A2_crdesc regs[] =
{
	{ NULL, NULL }
};

const A2_unitdesc a2_xinsert_unitdesc =
{
	"xinsert",			/* name */

	A2_MATCHIO | A2_XINSERT,	/* flags */

	regs,				/* registers */

	1, A2_MAXCHANNELS,		/* [min,max]inputs */
	1, A2_MAXCHANNELS,		/* [min,max]outputs */

	sizeof(A2_xinsert),		/* instancesize */
	xi_Initialize,			/* Initialize */
	xi_Deinitialize			/* Deinitialize */
};


/*---------------------------------------------------------
	Low level API for xinsert
---------------------------------------------------------*/

/* NOTE: These run in engine context, as responses to A2MT_* messages! */

A2_errors a2_XinsertAddClient(A2_state *st, A2_voice *v,
		A2_xinsert_client *xic)
{
	A2_unit *u;
	A2_xinsert *xi;
	A2_xinsert_client *c;

	/* Find first 'XINSERT' enabled unit */
	if(!(u = v->units))
		return A2_NOUNITS;	/* Voice has no units! --> */
	while(!(u->descriptor->flags & A2_XINSERT))
		if(!(u = u->next))
			return A2_NOXINSERT; /* No suitable unit found! --> */

	/* Add client last in list! */
	xi = a2_xinsert_cast(u);
	c = xi->clients;
	if(c)
	{
		while(c->next)
			c = c->next;
		c->next = xic;
	}
	else
		xi->clients = xic;
	xic->unit = xi;

	xi_set_process(u);

	return A2_OK;
}


A2_errors a2_XinsertRemoveClient(A2_state *st, A2_xinsert_client *xic)
{
	A2_errors res;

	if(xic->unit)
	{
		/* Detach from the list */
		A2_xinsert_client *c = xic->unit->clients;
		if(c != xic)
		{
			while(c->next && (c->next != xic))
				c = c->next;
			c->next = c->next->next;
		}
		else
			xic->unit->clients = xic->next;

		xi_set_process(&xic->unit->header);
	}

	/* Notify client that it's being removed */
	if((res = xic->callback(NULL, 0, 0, xic->userdata)))
		a2r_Error(st, res, "xinsert client; removal notification");

	/* Destroy entry in a suitable fashion for the engine context */
	if(st->config->flags & A2_REALTIME)
	{
		A2_apimessage am;
		am.b.common.action = A2MT_XICREMOVED;
		am.b.common.timestamp = st->now_ticks;
		am.b.xic.client = xic;
		return a2_writemsg(st->toapi, &am, A2_MSIZE(b.xic));
	}
	else
	{
		free(xic);
		return A2_OK;
	}
}
