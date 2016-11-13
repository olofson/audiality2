/*
 * api.c - Audiality 2 asynchronous API implementation
 *
 * Copyright 2010-2016 David Olofson <david@olofson.net>
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
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "internals.h"
#include "compiler.h"
#include "xinsert.h"
#include "pitch.h"


/*---------------------------------------------------------
	Global API resource management
---------------------------------------------------------*/

static A2_atomic a2_api_users = 0;
static A2_atomic a2_api_up = 0;
static A2_errors a2_api_error = A2_OK;


A2_errors a2_add_api_user(void)
{
	if(a2_AtomicAdd(&a2_api_users, 1) == 0)
	{
		A2_errors e;
		/* We could arrive here right when the API is being closed! */
		while(a2_AtomicAdd(&a2_api_up, 0))
			a2_Yield();
		a2_api_error = A2_OK;
		if((e = a2_time_open()) ||
				(e = a2_drivers_open()) ||
				(e = a2_units_open()) ||
				(e = a2_pitch_open()))
		{
			a2_api_error = e;
			a2_AtomicAdd(&a2_api_users, -1);
			return e;
		}
		a2_AtomicAdd(&a2_api_up, 1);
	}
	else
	{
		/* Someone beat us to it. Wait until the API is actually up! */
		while(!a2_AtomicAdd(&a2_api_up, 0))
		{
			if(a2_api_error)
			{
				/*
				 * Oh... The thread opening the API failed.
				 * We're not likely going to succeed either,
				 * so we return the same error code.
				 */
				a2_AtomicAdd(&a2_api_users, -1);
				return a2_api_error;
			}
			a2_Yield();
		}
	}
	return A2_OK;
}


void a2_remove_api_user(void)
{
	int users = a2_AtomicAdd(&a2_api_users, -1);
	if(users == 1)
	{
		/*
		 * If someone tries to reopen now, a2_add_api_user() will wait
		 * until we're done closing, before opening again.
		 */
		a2_pitch_close();
		a2_units_close();
		a2_drivers_close();
		a2_time_close();
		a2_AtomicAdd(&a2_api_up, -1);
	}
	else if(!users)
	{
		a2_AtomicAdd(&a2_api_users, 1);
		fprintf(stderr, "Audiality 2 INTERNAL ERROR: "
				"a2_remove_api_user() called while "
				"a2_api_users == 0!\n");
	}
}


/*---------------------------------------------------------
	Handle management
---------------------------------------------------------*/

A2_handle a2_RootVoice(A2_state *st)
{
	return st->rootvoice;
}


A2_otypes a2_TypeOf(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	return (A2_otypes)hi->typecode;
}


const char *a2_TypeName(A2_state *st, A2_otypes type)
{
	return rchm_TypeName(&st->ss->hm, type);
}


const char *a2_String(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi;
	char *sb = st->ss->strbuf;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return NULL;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
	  {
		A2_bank *b = (A2_bank *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<bank \"%s\" %p>", b->name, b);
		return sb;
	  }
	  case A2_TWAVE:
	  {
		A2_wave *w = (A2_wave *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<wave %p>", w);
		return sb;
	  }
	  case A2_TUNIT:
	  {
		const A2_unitdesc *ud = a2_GetUnitDescriptor(st, handle);
		snprintf(sb, A2_TMPSTRINGSIZE, "<unit '%s' %p>", ud->name, ud);
		return sb;
	  }
	  case A2_TPROGRAM:
	  {
		A2_program *p = (A2_program *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<program %p>", p);
		return sb;
	  }
	  case A2_TSTRING:
		return ((A2_string *)hi->d.data)->buffer;
	  case A2_TSTREAM:
	  {
		A2_stream *p = (A2_stream *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<stream %p>", p);
		return sb;
	  }
	  case A2_TXICLIENT:
	  {
		snprintf(sb, A2_TMPSTRINGSIZE, "<xinsert client %p>",
				hi->d.data);
		return sb;
	  }
	  case A2_TDETACHED:
		snprintf(sb, A2_TMPSTRINGSIZE, "<detached handle %d>", handle);
		return sb;
	  case A2_TNEWVOICE:
	  {
		snprintf(sb, A2_TMPSTRINGSIZE, "<new voice>");
		return sb;
	  }
	  case A2_TVOICE:
	  {
		A2_voice *v = (A2_voice *)hi->d.data;
		if(!v)
			return "<detached voice handle>";
		snprintf(sb, A2_TMPSTRINGSIZE, "<voice %p>", v);
		return sb;
	  }
	}
	return "<object of unknown type>";
}


const char *a2_Name(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return NULL;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
		return ((A2_bank *)hi->d.data)->name;
	  case A2_TUNIT:
	  {
		const A2_unitdesc *ud = a2_GetUnitDescriptor(st, handle);
		if(!ud)
			return NULL;
		return ud->name;
	  }
	  case A2_TWAVE:
	  case A2_TPROGRAM:
	  case A2_TSTRING:
	  case A2_TSTREAM:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TNEWVOICE:
	  case A2_TVOICE:
		return NULL;
	}
	return NULL;
}


int a2_Size(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
		/* NOTE: Exports only! Private symbols are excluded here. */
		return ((A2_bank *)hi->d.data)->exports.nitems;
	  case A2_TWAVE:
	  {
		A2_wave *w = (A2_wave *)hi->d.data;
		switch(w->type)
		{
		  case A2_WOFF:
		  case A2_WNOISE:
			return -A2_NOTIMPLEMENTED;
		  case A2_WWAVE:
		  case A2_WMIPWAVE:
			return w->d.wave.size[0];
		}
		return -(A2_INTERNAL + 30);
	  }
	  case A2_TSTRING:
		return ((A2_string *)hi->d.data)->length;
	  case A2_TPROGRAM:
	  {
	  	/* Calculate total code size ("words") of program */
		A2_program *p = (A2_program *)hi->d.data;
		int size = 0;
		int i;
		for(i = 0; i < p->nfuncs; ++i)
			size += p->funcs[i].size;
		return size;
	  }
	  case A2_TSTREAM:
	  {
		A2_stream *str;
		A2_errors res = a2_GetStream(st, handle, &str);
		if(res)
			return -res;
		if(str->Size)
			return str->Size(str);
		else
			return str->size;
	  }
	  case A2_TUNIT:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TNEWVOICE:
	  case A2_TVOICE:
		return -A2_NOTIMPLEMENTED;
	}
	return -(A2_INTERNAL + 31);
}


A2_errors a2_Retain(A2_state *st, A2_handle handle)
{
	return (A2_errors)rchm_Retain(&st->ss->hm, handle);
}


/*---------------------------------------------------------
	Async API message gateway
---------------------------------------------------------*/

A2_errors a2_OpenAPI(A2_state *st)
{
	int i;

	/* Initialize FIFOs for the API */
	float buffer = (float)st->config->buffer / st->config->samplerate;
	int nmessages = A2_MINMESSAGES + buffer * A2_TIMEMESSAGES;
	st->fromapi = sfifo_Open(nmessages * sizeof(A2_apimessage));
	st->toapi = sfifo_Open(nmessages * sizeof(A2_apimessage));
	if(!st->fromapi || !st->toapi)
	{
		fprintf(stderr, "Audiality 2: Could not open async API!\n");
		return A2_OOMEMORY;
	}

	/* Initialize event pool for internal realtime communication */
	if(st->config->eventpool >= 0)
		nmessages = st->config->eventpool;
	else
		nmessages = A2_MINEVENTS + buffer * A2_TIMEEVENTS;
	for(i = 0; i < nmessages; ++i)
	{
		A2_event *e = a2_NewEvent(st);
		if(!e)
		{
			fprintf(stderr, "Audiality 2: Could not initialize "
					"internal event pool!\n");
			return A2_OOMEMORY;
		}
		e->next = st->eventpool;
		st->eventpool = e;
	}
	EVLEAKTRACK(fprintf(stderr, "Audiality 2: Allocated %d events.\n",
			st->numevents);)
	return A2_OK;
}


void a2_CloseAPI(A2_state *st)
{
	if(st->fromapi)
	{
		sfifo_Close(st->fromapi);
		st->fromapi = NULL;
	}
	if(st->toapi)
	{
		sfifo_Close(st->toapi);
		st->toapi = NULL;
	}
	while(st->eventpool)
	{
		A2_event *e = st->eventpool;
		st->eventpool = e->next;
		st->sys->RTFree(st->sys, e);
		EVLEAKTRACK(--st->numevents;)
	}
	EVLEAKTRACK(if(st->numevents)
		fprintf(stderr, "Audiality 2: %d events leaked!\n",
				st->numevents);)
}


/*
 * Engine side message pump
 */

static inline void a2r_em_forwardevent(A2_state *st, A2_apimessage *am,
		unsigned latelimit)
{
	int tsdiff;
	A2_event *e;
	A2_event **eq = a2_GetEventQueue(st, am->target);
	if(!eq)
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_forwardevent()[1]");
		return;
	}
	if(!(e = a2_AllocEvent(st)))
	{
		a2r_Error(st, A2_OOMEMORY, "a2r_em_forwardevent()[2]");
		return;
	}
	memcpy(&e->b, &am->b, am->size - offsetof(A2_apimessage, b));
	if(am->size < A2_MSIZE(b.common.argc))
		e->b.common.argc = 0;
	tsdiff = a2_TSDiff(e->b.common.timestamp, latelimit);
	if(tsdiff < st->tsmin)
		st->tsmin = tsdiff;
	if(tsdiff > st->tsmax)
		st->tsmax = tsdiff;
	st->tssum += tsdiff >> 8;
	++st->tssamples;
	if(tsdiff < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n",
				(latelimit - e->b.common.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2r_em_forwardevent()[3]");
		e->b.common.timestamp = latelimit;
	}
	MSGTRACK(e->source = "a2r_em_forwardevent()";)
	a2_SendEvent(eq, e);
}

static inline void a2r_em_eocevent(A2_state *st, A2_apimessage *am)
{
	A2_event *e = a2_AllocEvent(st);
	if(!e)
	{
		a2r_Error(st, A2_OOMEMORY, "a2r_em_eocevent()[1]");
		return;
	}
	memcpy(&e->b, &am->b, am->size - offsetof(A2_apimessage, b));
	MSGTRACK(e->source = "a2r_em_eocevent()";)
	/* FIXME: Events are queued in reverse order here... */
	e->next = st->eocevents;
	st->eocevents = e;
}

void a2r_PumpEngineMessages(A2_state *st, unsigned latelimit)
{
	while(sfifo_Used(st->fromapi) >= A2_APIREADSIZE)
	{
		A2_apimessage am;
		if(sfifo_Read(st->fromapi, &am, (unsigned)A2_APIREADSIZE) < 0)
		{
			a2r_Error(st, A2_INTERNAL + 25,
					"Engine side FIFO read error");
			return;
		}
		if(am.size > A2_APIREADSIZE)
			if(sfifo_Read(st->fromapi,
					((char *)&am) + A2_APIREADSIZE,
					am.size - A2_APIREADSIZE) < 0)
			{
				a2r_Error(st, A2_INTERNAL + 26,
						"Engine side FIFO read error");
				return;
			}
		++st->apimessages;
		switch(am.b.common.action)
		{
		  case A2MT_PLAY:
		  case A2MT_START:
		  case A2MT_SEND:
		  case A2MT_SENDSUB:
		  case A2MT_KILL:
		  case A2MT_KILLSUB:
		  case A2MT_ADDXIC:
		  case A2MT_REMOVEXIC:
		  case A2MT_RELEASE:
			a2r_em_forwardevent(st, &am, latelimit);
			break;
		  case A2MT_WAHP:
			a2r_em_eocevent(st, &am);
			break;
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unknown API message "
					"%d!\n", am.b.common.action);
			break;
#endif
		}
	}
}


static inline void a2_detach_or_free_handle(A2_state *st, A2_handle h)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, h);
	if(hi)
	{
		if(hi->refcount)
			hi->typecode = A2_TDETACHED;
		else
			rchm_Free(&st->ss->hm, h);
	}
}


/*
 * API side message pump
 */

void a2_PumpMessages(A2_state *st)
{
	while(sfifo_Used(st->toapi) >= A2_APIREADSIZE)
	{
		A2_apimessage am;
		if(sfifo_Read(st->toapi, &am, (unsigned)A2_APIREADSIZE) < 0)
		{
			fprintf(stderr, "Audiality 2: API side FIFO read"
					" error! (27)\n");
			return;
		}
		if(am.size > A2_APIREADSIZE)
			if(sfifo_Read(st->toapi, (char *)&am + A2_APIREADSIZE,
					am.size - A2_APIREADSIZE) < 0)
			{
				fprintf(stderr, "Audiality 2: API side FIFO"
						" read error! (28)\n");
				return;
			}
		if(am.size < A2_MSIZE(b.common.argc))
			am.b.common.argc = 0;
		switch(am.b.common.action)
		{
		  case A2MT_DETACH:
			a2_detach_or_free_handle(st, am.target);
			break;
		  case A2MT_XICREMOVED:
		  {
			A2_xinsert_client *c = am.b.xic.client;
			a2_detach_or_free_handle(st, c->handle);
			if(c->stream)
				a2_DetachStream(st, c->stream);
			if(c->fifo)
				sfifo_Close(c->fifo);
			free(c);
			break;
		  }
		  case A2MT_ERROR:
			fprintf(stderr, "Audiality 2: [RT] %s (%s)\n",
					a2_ErrorString(am.b.error.code),
					am.b.error.info);
			break;
		  case A2MT_WAHP:
		  {
			A2_wahp_entry *we = am.b.wahp.entry;
			if(!--we->count)
			{
				/* Last response! Let's make the callback. */
				we->callback(we->state, we->userdata);
				free(we);
			}
			break;
		  }
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unknown engine message "
					"%d!\n", am.b.common.action);
			break;
#endif
		}
	}
}


void a2r_ProcessEOCEvents(A2_state *st, unsigned frames)
{
	/*
	 * We don't count it as a cycle unless samples were processed!
	 *
	 * NOTE: This is to make sure A2MT_WAHP works as intended. We may have
	 *       have to change this if we add other EOC events later.
	 */
	if(!frames)
		return;

	while(st->eocevents)
	{
		A2_event *e = st->eocevents;
		switch(e->b.common.action)
		{
		  case A2MT_WAHP:
		  {
		  	/* Just send it back as is to the API context! */
		  	A2_apimessage am;
		  	int ms = A2_MSIZE(b.wahp);
			memcpy(&am.b, &e->b, ms - offsetof(A2_apimessage, b));
			a2_writemsg(st->toapi, &am, ms);
			break;
		  }
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unexpected message "
					"%d in a2r_ProcessEOCEvents()!\n",
					e->b.common.action);
			break;
#endif
		}
		st->eocevents = e->next;
		a2_FreeEvent(st, e);
	}
}


A2_errors a2_WhenAllHaveProcessed(A2_state *st, A2_generic_cb cb,
		void *userdata)
{
	A2_apimessage am;
	A2_state *pstate = st->parent ? st->parent : st;
	A2_wahp_entry *we = (A2_wahp_entry *)malloc(sizeof(A2_wahp_entry));
	if(!we)
		return A2_OOMEMORY;
	we->state = st;
	we->callback = cb;
	we->userdata = userdata;
	we->count = 0;
	for(st = pstate; st; st = st->next)
		if(st->fromapi)
			++we->count;
	if(we->count)
	{
		am.b.common.action = A2MT_WAHP;
		am.b.wahp.entry = we;
		for(st = pstate; st; st = st->next)
			if(st->fromapi)
				a2_writemsg(st->fromapi, &am,
						A2_MSIZE(b.wahp));
	}
	else
	{
		/* Emergency: No functional engine states present! */
		we->callback(we->state, we->userdata);
		free(we);
	}
	return A2_OK;
}


/* Post error message to the API from engine context */
A2_errors a2r_Error(A2_state *st, A2_errors e, const char *info)
{
	st->last_rt_error = e;
	if(st->config->flags & A2_RTSILENT)
		return A2_OK;
	if(st->config->flags & A2_REALTIME)
	{
		A2_apimessage am;
		am.b.common.action = A2MT_ERROR;
		am.b.common.timestamp = st->now_ticks;
		am.b.error.code = e;
		am.b.error.info = info;
		return a2_writemsg(st->toapi, &am, A2_MSIZE(b.error));
	}
	else
	{
		fprintf(stderr, "Audiality 2: [engine] %s (%s)\n",
				a2_ErrorString(e), info);
		return A2_OK;
	}
}


/*
 * Send a message to the API context regarding handle 'h', telling it to either
 * free it immediately (refcount == 0), or to change its type to A2_TDETACHED,
 * so it can be released later.
 */
void a2r_DetachHandle(A2_state *st, A2_handle h)
{
	A2_apimessage am;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, h);
	if(!hi)
		return;
	if(!hi->typecode)
		return;
	if(!st->toapi)
		return;
	/* Respond back to the API: "Clear to free the handle!" */
	am.b.common.action = A2MT_DETACH;
	am.target = h;
	/* NOTE: No timestamp on this one, so we stop at the 'action' field! */
	a2_writemsg(st->toapi, &am, A2_MSIZE(b.common.action));
}


A2_errors a2_Release(A2_state *st, A2_handle handle)
{
	A2_errors res = -rchm_Release(&st->ss->hm, handle);
	if(res == A2_REFUSE)
	{
		/*
		 * Special hack to deal with objects that need an engine
		 * round trip for cleanup. Destructors only have access to the
		 * master state - not the actual owner state provided through
		 * 'st' here, which we need to get the correct message FIFO!
		 */
		RCHM_handleinfo *hi = rchm_Locate(&st->ss->hm, handle);
		switch((A2_otypes)hi->typecode)
		{
		  case A2_TNEWVOICE:
		  case A2_TVOICE:
		  case A2_TXICLIENT:
		  {
			A2_apimessage am;
			a2_PumpMessages(st);
			if(!(st->config->flags & A2_TIMESTAMP))
				a2_TimestampReset(st);
			am.b.common.timestamp = st->timestamp;
			am.target = handle;
			if(hi->typecode == A2_TXICLIENT)
				am.b.common.action = A2MT_REMOVEXIC;
			else
				am.b.common.action = A2MT_RELEASE;
			a2_writemsg(st->fromapi, &am, A2_MSIZE(b.common));
			break;
		  }
		  case A2_TBANK:
		  case A2_TUNIT:
		  case A2_TWAVE:
		  case A2_TPROGRAM:
		  case A2_TSTRING:
		  case A2_TSTREAM:
		  case A2_TDETACHED:
			break;
		}
	}
	return res;
}


/*---------------------------------------------------------
	Utilities
---------------------------------------------------------*/

float a2_Rand(A2_state *st, float max)
{
	return a2_Noise(&st->noisestate) * max / 65536.0f;
}


/*---------------------------------------------------------
	Timestamping
---------------------------------------------------------*/

A2_timestamp a2_TimestampNow(A2_state *st)
{
	unsigned nf;
	int dt;
	if(!(st->config->flags & A2_REALTIME))
		return st->now_frames;

	do {
		nf = st->now_frames;
		dt = st->now_ticks;
	} while(nf != st->now_guard);
	dt = a2_GetTicks() - dt + st->tsmargin;
	if(dt < 0)
		dt = 0;	/* Audio has been off for a looooong time... */
	return nf + ((int64_t)st->msdur * dt >> 8);
}


A2_timestamp a2_TimestampGet(A2_state *st)
{
	return st->timestamp;
}


A2_timestamp a2_TimestampSet(A2_state *st, A2_timestamp ts)
{
	A2_timestamp oldts = st->timestamp;
#if DEBUG
	if(a2_TSDiff(ts, st->timestamp) < 0)
		fprintf(stderr, "Audiality 2: API timestamp moved %f frames "
				"backwards by a2_TimestampSet()!\n",
				a2_TSDiff(ts, st->timestamp) / -256.0f);
#endif
	st->timestamp = ts;
	return oldts;
}


int a2_ms2Timestamp(A2_state *st, double t)
{
	return (unsigned)(st->msdur * t / 256.0f);
}


double a2_Timestamp2ms(A2_state *st, int ts)
{
	return ts * 256.0f / st->msdur;
}


A2_timestamp a2_TimestampReset(A2_state *st)
{
	return a2_TimestampSet(st, a2_TimestampNow(st));
}


A2_timestamp a2_TimestampBump(A2_state *st, int dt)
{
	A2_timestamp oldts = st->timestamp;
	dt += st->nudge_adjust;
	if(dt < 0)
	{
		st->nudge_adjust = dt;
		dt = 0;
	}
	else
		st->nudge_adjust = 0;
	st->timestamp += dt;
#if DEBUG
	if(a2_TSDiff(st->timestamp, oldts) < 0)
		fprintf(stderr, "Audiality 2: API timestamp moved %f frames "
				"backwards by a2_TimestampBump()!\n",
				a2_TSDiff(st->timestamp, oldts) / -256.0f);
#endif
	return oldts;
}


int a2_TimestampNudge(A2_state *st, int offset, float amount)
{
	A2_timestamp intended = a2_TimestampNow(st) - offset;
#if DEBUG
	if((amount < 0.0f) || (amount > 1.0f))
		fprintf(stderr, "Audiality 2: a2_TimestampNudge() 'amount' is "
				"%f, but should be in [0, 1]!\n", amount);
#endif
	st->nudge_adjust = a2_TSDiff(intended, st->timestamp) * amount;
	return st->nudge_adjust;
}


/*---------------------------------------------------------
	Playing and controlling
---------------------------------------------------------*/

A2_handle a2_NewGroup(A2_state *st, A2_handle parent)
{
	return a2_Starta(st, parent, a2_Get(st, A2_ROOTBANK, "a2_groupdriver"),
			0, NULL);
}


A2_handle a2_Starta(A2_state *st, A2_handle parent, A2_handle program,
		unsigned argc, int *argv)
{
	A2_errors res;
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_TimestampReset(st);
	am.target = parent;
	am.b.common.action = A2MT_START;
	am.b.common.timestamp = st->timestamp;
	am.b.start.program = program;
	if((am.b.start.voice = rchm_New(&st->ss->hm, NULL, A2_TNEWVOICE)) < 0)
		return am.b.start.voice;
	if((res = a2_writemsgargs(st->fromapi, &am, argc, argv,
			offsetof(A2_apimessage, b.start.a))))
		return res;
	return am.b.start.voice;
}


A2_errors a2_Playa(A2_state *st, A2_handle parent, A2_handle program,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_TimestampReset(st);
	am.target = parent;
	am.b.common.action = A2MT_PLAY;
	am.b.common.timestamp = st->timestamp;
	am.b.play.program = program;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv,
				offsetof(A2_apimessage, b.play.a));
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.play.program));
}


A2_errors a2_Senda(A2_state *st, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if(ep >= A2_MAXEPS)
		return A2_INDEXRANGE;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_TimestampReset(st);
	am.target = voice;
	am.b.common.action = A2MT_SEND;
	am.b.common.timestamp = st->timestamp;
	am.b.play.program = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv,
				offsetof(A2_apimessage, b.play.a));
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.play.program));
}


A2_errors a2_SendSuba(A2_state *st, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if(ep >= A2_MAXEPS)
		return A2_INDEXRANGE;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_TimestampReset(st);
	am.target = voice;
	am.b.common.action = A2MT_SENDSUB;
	am.b.common.timestamp = st->timestamp;
	am.b.play.program = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv,
				offsetof(A2_apimessage, b.play.a));
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.play.program));
}


A2_errors a2_Kill(A2_state *st, A2_handle voice)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_TimestampReset(st);
	am.target = voice;
	am.b.common.action = A2MT_KILL;
	am.b.common.timestamp = st->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.common));
}


A2_errors a2_KillSub(A2_state *st, A2_handle voice)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_TimestampReset(st);
	am.target = voice;
	am.b.common.action = A2MT_KILLSUB;
	am.b.common.timestamp = st->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.common));
}


/*---------------------------------------------------------
	Handle types for API objects
---------------------------------------------------------*/

static RCHM_errors a2_VoiceDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	return RCHM_REFUSE;
}

A2_errors a2_RegisterAPITypes(A2_state *st)
{
	A2_errors res = a2_RegisterType(st, A2_TNEWVOICE, "newvoice",
			a2_VoiceDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TVOICE, "voice",
			a2_VoiceDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TDETACHED, "detached",
			NULL, NULL);
	return res;
}
