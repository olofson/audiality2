/*
 * xinsertapi.c - Audiality 2 xinsert callback and buffered stream APIs
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

#include <stdlib.h>
#include "internals.h"
#include "xinsert.h"


/* Pass client to the engine context. Return a handle for the client. */
static A2_handle a2_add_xic(A2_state *st, A2_handle voice,
		A2_xinsert_client *xic)
{
	A2_errors res;
	A2_apimessage am;

	/*
	 * We need this here in case we have to destroy the XIC again before
	 * the A2MT_ADDXIC has been processed, because xic->unit will be NULL
	 * until then!
	 */
	xic->voice = voice;

	/* Create a handle for the client (both streams and callbacks!) */
	xic->handle = rchm_NewEx(&st->ss->hm, xic, A2_TXICLIENT, 0, 1);
	if(xic->handle < 0)
		return xic->handle;

	/* Tell engine context to install the client! */
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	am.target = voice;
	am.b.common.action = A2MT_ADDXIC;
	am.b.common.timestamp = st->timestamp;
	am.b.xic.client = xic;
	res = a2_writemsg(st->fromapi, &am, A2_MSIZE(b.xic));
	if(res)
	{
		rchm_Free(&st->ss->hm, xic->handle);
		return -res;
	}
	return xic->handle;
}


/*---------------------------------------------------------
	Low level API for xsink/xsource/xinsert
---------------------------------------------------------*/

/* NOTE: These run in engine context, as responses to A2MT_* messages! */

A2_errors a2_XinsertAddClient(A2_state *st, A2_voice *v,
		A2_xinsert_client *xic)
{
	A2_unit *u;
	A2_xinsert *xi;
	A2_xinsert_client *c;

	/* Find first 'XINSERT' enabled unit with compatible I/O setup */
	if(!(u = v->units))
		return A2_NOUNITS;	/* Voice has no units! --> */
	for( ; u; u = u->next)
	{
		if(!(u->descriptor->flags & A2_XINSERT))
			continue;
		if((xic->flags & A2_XI_READ) && !u->ninputs)
			continue;
		if((xic->flags & A2_XI_WRITE) && !u->noutputs)
			continue;
		break;
	}
	if(!u)
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

	xi->SetProcess(u);

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

		xic->unit->SetProcess(&xic->unit->header);
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


/*---------------------------------------------------------
	Callback xinsert interface
---------------------------------------------------------*/

A2_handle a2_SinkCallback(A2_state *st, A2_handle voice,
		A2_xinsert_cb callback, void *userdata)
{
	A2_handle h;
	A2_xinsert_client *xic = (A2_xinsert_client *)calloc(1,
			sizeof(A2_xinsert_client));
	if(!xic)
		return -A2_OOMEMORY;
	xic->callback = callback;
	xic->userdata = userdata;
	xic->flags = A2_XI_READ;
	if((h = a2_add_xic(st, voice, xic)) < 0)
		free(xic);
	return h;
}


A2_handle a2_SourceCallback(A2_state *st, A2_handle voice,
		A2_xinsert_cb callback, void *userdata)
{
	A2_handle h;
	A2_xinsert_client *xic = (A2_xinsert_client *)calloc(1,
			sizeof(A2_xinsert_client));
	if(!xic)
		return -A2_OOMEMORY;
	xic->callback = callback;
	xic->userdata = userdata;
	xic->flags = A2_XI_WRITE;
	if((h = a2_add_xic(st, voice, xic)) < 0)
		free(xic);
	return h;
}


A2_handle a2_InsertCallback(A2_state *st, A2_handle voice,
		A2_xinsert_cb callback, void *userdata)
{
	A2_handle h;
	A2_xinsert_client *xic = (A2_xinsert_client *)calloc(1,
			sizeof(A2_xinsert_client));
	if(!xic)
		return -A2_OOMEMORY;
	xic->callback = callback;
	xic->userdata = userdata;
	xic->flags = A2_XI_READ | A2_XI_WRITE;
	if((h = a2_add_xic(st, voice, xic)) < 0)
		free(xic);
	return h;
}


/*---------------------------------------------------------
	Buffered stream xinsert interface
---------------------------------------------------------*/

static A2_handle a2_open_xic_stream(A2_state *st, A2_handle voice,
		int channel, int size, unsigned flags,
		A2_xinsert_cb callback, unsigned xiflags)
{
	A2_handle h, sh;

	/* First, add a client... */
	A2_xinsert_client *xic = (A2_xinsert_client *)calloc(1,
			sizeof(A2_xinsert_client));
	if(!xic)
		return -A2_OOMEMORY;
	xic->callback = callback;
	xic->userdata = xic;
	xic->flags = A2_XI_STREAM | xiflags;
	if(flags & A2_RTSILENT)
		xic->flags |= A2_XI_SILENT;
	if((h = a2_add_xic(st, voice, xic)) < 0)
	{
		free(xic);
		return h;
	}

	/* Then open a stream on it! */
	sh = a2_OpenStream(st, h, channel, size, flags);

	/* NOTE: This also does the right thing if a2_OpenStream() fails! */
	a2_Release(st, h);	/* Owned only by the stream! */
	return sh;
}


static A2_errors a2_sinkstream_process(int **buffers, unsigned nbuffers,
		unsigned frames, void *userdata)
{
	int res;
	void *data;
	A2_xinsert_client *xic = (A2_xinsert_client *)userdata;
	int size = frames * sizeof(int32_t);
	if(!buffers)
		return A2_OK;	/* "removed" notification - nothing to do! */
/* FIXME: This should handle partial buffers! */
	if(sfifo_Space(xic->fifo) < size)
	{
		/* Not enough space! */
		if((xic->flags & A2_XI_SILENT) || xic->xflow)
			return A2_OK;
		else
		{
			xic->xflow = 1;
			return A2_BUFOVERFLOW;
		}
	}
	xic->xflow = 0;
	data = buffers[xic->channel];
	if((res = sfifo_Write(xic->fifo, data, size)) != size)
	{
		if(res == SFIFO_CLOSED)
			return A2_INTERNAL + 521;
		else
			return A2_INTERNAL + 522;
	}
	return A2_OK;
}

A2_handle a2_OpenSink(A2_state *st, A2_handle voice,
		int channel, int size, unsigned flags)
{
	return a2_open_xic_stream(st, voice, channel, size, flags,
			a2_sinkstream_process, A2_XI_READ);
}


static A2_errors a2_sourcestream_process(int **buffers, unsigned nbuffers,
		unsigned frames, void *userdata)
{
	int res;
	void *data;
	A2_xinsert_client *xic = (A2_xinsert_client *)userdata;
	int size = frames * sizeof(int32_t);
	if(!buffers)
		return A2_OK;	/* "removed" notification - nothing to do! */
	data = buffers[xic->channel];
/* FIXME: This should handle partial buffers! */
	if(sfifo_Used(xic->fifo) < size)
	{
		/* Not enough data! */
		memset(data, 0, size);
		if((xic->flags & A2_XI_SILENT) || xic->xflow)
			return A2_OK;
		else
		{
			xic->xflow = 1;
			return A2_BUFUNDERFLOW;
		}
	}
	xic->xflow = 0;
	if((res = sfifo_Read(xic->fifo, data, size)) != size)
	{
		if(res == SFIFO_CLOSED)
			return A2_INTERNAL + 531;
		else
			return A2_INTERNAL + 532;
	}
	return A2_OK;
}

A2_handle a2_OpenSource(A2_state *st, A2_handle voice,
		int channel, int size, unsigned flags)
{
	return a2_open_xic_stream(st, voice, channel, size, flags,
			a2_sourcestream_process, A2_XI_WRITE);
}


/*---------------------------------------------------------
	Object handle type and stream interface
---------------------------------------------------------*/

static A2_errors xi_stream_read(A2_stream *str,
		A2_sampleformats fmt, void *data, unsigned size)
{
	int res;
	A2_xinsert_client *xic = (A2_xinsert_client *)str->targetobject;
	if(sfifo_Used(xic->fifo) < size)
		return A2_BUFUNDERFLOW;
/*HACK*/
	if(fmt != A2_I24)
		return A2_WRONGFORMAT;
/*/HACK*/
	if((res = sfifo_Read(xic->fifo, data, size) != size))
	{
		if(res == SFIFO_CLOSED)
			return A2_INTERNAL + 501;
		else
			return A2_INTERNAL + 502;
	}
	return A2_OK;
}

static A2_errors xi_stream_write(A2_stream *str,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	int res;
	A2_xinsert_client *xic = (A2_xinsert_client *)str->targetobject;
	if(sfifo_Space(xic->fifo) < size)
		return A2_BUFOVERFLOW;
/*HACK*/
	if(fmt != A2_I24)
		return A2_WRONGFORMAT;
/*/HACK*/
	if((res = sfifo_Write(xic->fifo, data, size) != size))
	{
		if(res == SFIFO_CLOSED)
			return A2_INTERNAL + 511;
		else
			return A2_INTERNAL + 512;
	}
	return A2_OK;
}


static int xi_stream_available(A2_stream *str)
{
	A2_xinsert_client *xic = (A2_xinsert_client *)str->targetobject;
/* HACK: A2_I24 format only! */
	return sfifo_Used(xic->fifo) / sizeof(int32_t);
}


static int xi_stream_space(A2_stream *str)
{
	A2_xinsert_client *xic = (A2_xinsert_client *)str->targetobject;
/* HACK: A2_I24 format only! */
	return sfifo_Space(xic->fifo) / sizeof(int32_t);
}


static A2_errors xi_stream_flush(A2_stream *str)
{
	A2_xinsert_client *xic = (A2_xinsert_client *)str->targetobject;
	sfifo_Flush(xic->fifo);
	return A2_OK;
}


/* OpenStream() method for A2_TXICLIENT objects (with the A2_XI_STREAM flag) */
static A2_errors xi_stream_open(A2_stream *str, A2_handle h)
{
	A2_xinsert_client *xic;
	RCHM_handleinfo *hi = rchm_Get(&str->state->ss->hm, str->targethandle);
	if(!hi)
		return A2_INVALIDHANDLE;
	if(!hi->refcount)
		return A2_DEADHANDLE;
	xic = (A2_xinsert_client *)hi->d.data;
	if(!(xic->flags & A2_XI_STREAM))
		return A2_NOSTREAMCLIENT;
	if(xic->channel < 0)
		return A2_NOTIMPLEMENTED;
	if(xic->flags & A2_XI_WRITE)
		str->Write = xi_stream_write;
	else if(xic->flags & A2_XI_READ)
	{
		str->Read = xi_stream_read;
		str->Flush = xi_stream_flush;
	}
	else
		return A2_INTERNAL + 500;
	str->Available = xi_stream_available;
	str->Space = xi_stream_space;
	if(str->size <= 0)
		return A2_VALUERANGE;
	if(!(xic->fifo = sfifo_Open(str->size * sizeof(int32_t))))
		return A2_OOMEMORY;
	str->size = xic->fifo->size / sizeof(int32_t);	/* Actual size! */
	xic->channel = str->channel;
	xic->stream = h;
	return A2_OK;
}


/*
 * NOTE: Just like voices, these objects can be killed by the engine context
 *       at any time, so we need to use the "round-trip with detached handles"
 *       method here as well. Unlike voices, we tell the engine to remove
 *       xinsert clients as their handles are (attempting to be) released.
 */
static RCHM_errors xi_destructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	A2_apimessage am;
	A2_xinsert_client *xic = (A2_xinsert_client *)hi->d.data;
	A2_state *st = ((A2_typeinfo *)ti)->state;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	am.target = xic->voice;
	am.b.common.action = A2MT_REMOVEXIC;
	am.b.common.timestamp = st->timestamp;
	am.b.xic.client = xic;
	a2_writemsg(st->fromapi, &am, A2_MSIZE(b.xic));
	return RCHM_REFUSE;
}


A2_errors a2_RegisterXICTypes(A2_state *st)
{
	return a2_RegisterType(st, A2_TXICLIENT, "xiclient", xi_destructor,
			xi_stream_open);
}
