/*
 * interface.c - Audiality 2 Interface implementation
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
#include "internals.h"


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
	while(st->interfaces)
		a2_RemoveInterface(st->interfaces);
}


/*---------------------------------------------------------
	Engine side message pump
---------------------------------------------------------*/

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


/*---------------------------------------------------------
	API side message pump
---------------------------------------------------------*/

void a2_PumpMessages(A2_interface *i)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;

	if(ii->flags & A2_REALTIME)
		return;

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
	if(st)
	{
		st->last_rt_error = e;
		if(st->config->flags & A2_RTSILENT)
			return A2_OK;
	}
	if(st && (st->config->flags & A2_REALTIME))
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


static A2_errors a2_API_Release(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
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
			a2_PumpMessages(i);
			if(!(ii->flags & A2_TIMESTAMP))
				a2_TimestampReset(i);
			am.b.common.timestamp = ii->timestamp;
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
		  case A2_TCONSTANT:
		  case A2_TSTRING:
		  case A2_TSTREAM:
		  case A2_TDETACHED:
			break;
		}
	}
	return res;
}


static A2_errors a2_RT_Release(A2_interface *i, A2_handle handle)
{
	/*
	 * TODO:
	 *	Sort of like the second half of the API version for A2_REFUSE
	 *	handles, but for all object types: Do any engine side cleanup,
	 *	and then send an A2MT_RELEASE or similar to the API side.
	 */
	return A2_NOTIMPLEMENTED;
}


/*---------------------------------------------------------
	Timestamping
---------------------------------------------------------*/

/*----- API context implementation ----------------------*/

static A2_timestamp a2_API_TimestampNow(A2_interface *i)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	unsigned nf;
	int dt;
	if(!(st->config->flags & A2_REALTIME))
		return st->now_frames;

	do {
		nf = st->now_frames;
		dt = st->now_ticks;
	} while(nf != st->now_guard);
	dt = a2_GetTicks() - dt + ii->tsmargin;
	if(dt < 0)
		dt = 0;	/* Audio has been off for a looooong time... */
	return nf + ((int64_t)st->msdur * dt >> 8);
}


static int a2_API_TimestampNudge(A2_interface *i, int offset, float amount)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_timestamp intended = a2_API_TimestampNow(i) - offset;
#if DEBUG
	if((amount < 0.0f) || (amount > 1.0f))
		fprintf(stderr, "Audiality 2: a2_TimestampNudge() 'amount' is "
				"%f, but should be in [0, 1]!\n", amount);
#endif
	ii->nudge_adjust = a2_TSDiff(intended, ii->timestamp) * amount;
	return ii->nudge_adjust;
}


/*----- Engine context implementation -------------------*/

static A2_timestamp a2_RT_TimestampNow(A2_interface *i)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return st->now_fragstart;
}


static int a2_RT_TimestampNudge(A2_interface *i, int offset, float amount)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_timestamp intended = st->now_fragstart - offset;
#if DEBUG
	if((amount < 0.0f) || (amount > 1.0f))
		fprintf(stderr, "Audiality 2: a2_TimestampNudge() 'amount' is "
				"%f, but should be in [0, 1]!\n", amount);
#endif
	ii->nudge_adjust = a2_TSDiff(intended, ii->timestamp) * amount;
	return ii->nudge_adjust;
}


/*----- Common implementation ---------------------------*/

static A2_timestamp a2_common_TimestampGet(A2_interface *i)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	return ii->timestamp;
}


static int a2_common_ms2Timestamp(A2_interface *i, double t)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return (unsigned)(st->msdur * t / 256.0f);
}


static double a2_common_Timestamp2ms(A2_interface *i, int ts)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return ts * 256.0f / st->msdur;
}


static A2_timestamp a2_common_TimestampSet(A2_interface *i, A2_timestamp ts)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_timestamp oldts = ii->timestamp;
#if DEBUG
	if(a2_TSDiff(ts, ii->timestamp) < 0)
		fprintf(stderr, "Audiality 2: API timestamp moved %f frames "
				"backwards by a2_TimestampSet()!\n",
				a2_TSDiff(ts, ii->timestamp) / -256.0f);
#endif
	ii->timestamp = ts;
	return oldts;
}


static A2_timestamp a2_common_TimestampBump(A2_interface *i, int dt)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_timestamp oldts = ii->timestamp;
	dt += ii->nudge_adjust;
	if(dt < 0)
	{
		ii->nudge_adjust = dt;
		dt = 0;
	}
	else
		ii->nudge_adjust = 0;
	ii->timestamp += dt;
#if DEBUG
	if(a2_TSDiff(ii->timestamp, oldts) < 0)
		fprintf(stderr, "Audiality 2: API timestamp moved %f frames "
				"backwards by a2_TimestampBump()!\n",
				a2_TSDiff(ii->timestamp, oldts) / -256.0f);
#endif
	return oldts;
}


/*---------------------------------------------------------
	Playing and controlling
---------------------------------------------------------*/

/*----- API context implementation ----------------------*/

static A2_handle a2_API_Starta(A2_interface *i, A2_handle parent,
		A2_handle program, unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_errors res;
	A2_apimessage am;
	if(!(ii->flags & A2_TIMESTAMP))
		a2_TimestampReset(i);
	am.target = parent;
	am.b.common.action = A2MT_START;
	am.b.common.timestamp = ii->timestamp;
	am.b.start.program = program;
	if((am.b.start.voice = rchm_New(&st->ss->hm, NULL, A2_TNEWVOICE)) < 0)
		return am.b.start.voice;
	if((res = a2_writemsgargs(st->fromapi, &am, argc, argv,
			offsetof(A2_apimessage, b.start.a))))
		return res;
	return am.b.start.voice;
}


static A2_errors a2_API_Playa(A2_interface *i, A2_handle parent,
		A2_handle program, unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_apimessage am;
	if(!(ii->flags & A2_TIMESTAMP))
		a2_TimestampReset(i);
	am.target = parent;
	am.b.common.action = A2MT_PLAY;
	am.b.common.timestamp = ii->timestamp;
	am.b.play.program = program;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv,
				offsetof(A2_apimessage, b.play.a));
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.play.program));
}


static A2_errors a2_API_Senda(A2_interface *i, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_apimessage am;
	if(ep >= A2_MAXEPS)
		return A2_INDEXRANGE;
	if(!(ii->flags & A2_TIMESTAMP))
		a2_TimestampReset(i);
	am.target = voice;
	am.b.common.action = A2MT_SEND;
	am.b.common.timestamp = ii->timestamp;
	am.b.play.program = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv,
				offsetof(A2_apimessage, b.play.a));
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.play.program));
}


static A2_errors a2_API_SendSuba(A2_interface *i, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_apimessage am;
	if(ep >= A2_MAXEPS)
		return A2_INDEXRANGE;
	if(!(ii->flags & A2_TIMESTAMP))
		a2_TimestampReset(i);
	am.target = voice;
	am.b.common.action = A2MT_SENDSUB;
	am.b.common.timestamp = ii->timestamp;
	am.b.play.program = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv,
				offsetof(A2_apimessage, b.play.a));
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.play.program));
}


static A2_errors a2_API_Kill(A2_interface *i, A2_handle voice)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_apimessage am;
	if(!(ii->flags & A2_TIMESTAMP))
		a2_TimestampReset(i);
	am.target = voice;
	am.b.common.action = A2MT_KILL;
	am.b.common.timestamp = ii->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.common));
}


static A2_errors a2_API_KillSub(A2_interface *i, A2_handle voice)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_apimessage am;
	if(!(ii->flags & A2_TIMESTAMP))
		a2_TimestampReset(i);
	am.target = voice;
	am.b.common.action = A2MT_KILLSUB;
	am.b.common.timestamp = ii->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.common));
}


/*----- Engine context implementation -------------------*/

static A2_handle a2_RT_Starta(A2_interface *i, A2_handle parent,
		A2_handle program, unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_event *e;
	A2_handle vh;
	A2_event **eq = a2_GetEventQueue(st, parent);
	if(!eq)
		return -A2_BADVOICE;
	if(argc > A2_MAXARGS)
		return -A2_MANYARGS;
	/*
	 * FIXME: rchm_New() is not thread safe, so we can only use it on
	 * FIXME: off-line states running in the API context!
	 */
	if(st->config->flags & A2_REALTIME)
		return -A2_NOTIMPLEMENTED;
	if((vh = rchm_New(&st->ss->hm, NULL, A2_TNEWVOICE)) < 0)
		return vh;
	if(!(e = a2_AllocEvent(st)))
		return -A2_OOMEMORY;
	e->b.common.action = A2MT_START;
	if(ii->flags & A2_TIMESTAMP)
		e->b.common.timestamp = ii->timestamp;
	else
		e->b.common.timestamp = st->now_fragstart;
	e->b.common.argc = argc;
	e->b.start.program = program;
	e->b.start.voice = vh;
	memcpy(&e->b.start.a, argv, argc * sizeof(int));
	a2_SendEvent(eq, e);
	return vh;
}


static A2_errors a2_RT_Playa(A2_interface *i, A2_handle parent,
		A2_handle program, unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_event *e;
	A2_event **eq = a2_GetEventQueue(st, parent);
	if(!eq)
		return A2_BADVOICE;
	if(argc > A2_MAXARGS)
		return A2_MANYARGS;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	e->b.common.action = A2MT_PLAY;
	if(ii->flags & A2_TIMESTAMP)
		e->b.common.timestamp = ii->timestamp;
	else
		e->b.common.timestamp = st->now_fragstart;
	e->b.common.argc = argc;
	e->b.play.program = program;
	memcpy(&e->b.play.a, argv, argc * sizeof(int));
	a2_SendEvent(eq, e);
	return A2_OK;
}


static A2_errors a2_RT_Senda(A2_interface *i, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_event *e;
	A2_event **eq = a2_GetEventQueue(st, voice);
	if(!eq)
		return A2_BADVOICE;
	if(ep >= A2_MAXEPS)
		return A2_INDEXRANGE;
	if(argc > A2_MAXARGS)
		return A2_MANYARGS;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	e->b.common.action = A2MT_SEND;
	if(ii->flags & A2_TIMESTAMP)
		e->b.common.timestamp = ii->timestamp;
	else
		e->b.common.timestamp = st->now_fragstart;
	e->b.common.argc = argc;
	e->b.play.program = ep;
	memcpy(&e->b.play.a, argv, argc * sizeof(int));
	a2_SendEvent(eq, e);
	return A2_OK;
}


static A2_errors a2_RT_SendSuba(A2_interface *i, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_event *e;
	A2_event **eq = a2_GetEventQueue(st, voice);
	if(!eq)
		return A2_BADVOICE;
	if(ep >= A2_MAXEPS)
		return A2_INDEXRANGE;
	if(argc > A2_MAXARGS)
		return A2_MANYARGS;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	e->b.common.action = A2MT_SENDSUB;
	if(ii->flags & A2_TIMESTAMP)
		e->b.common.timestamp = ii->timestamp;
	else
		e->b.common.timestamp = st->now_fragstart;
	e->b.common.argc = argc;
	e->b.play.program = ep;
	memcpy(&e->b.play.a, argv, argc * sizeof(int));
	a2_SendEvent(eq, e);
	return A2_OK;
}


static A2_errors a2_RT_Kill(A2_interface *i, A2_handle voice)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_event *e;
	A2_event **eq = a2_GetEventQueue(st, voice);
	if(!eq)
		return A2_BADVOICE;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	e->b.common.action = A2MT_KILL;
	if(ii->flags & A2_TIMESTAMP)
		e->b.common.timestamp = ii->timestamp;
	else
		e->b.common.timestamp = st->now_fragstart;
	a2_SendEvent(eq, e);
	return A2_OK;
}


static A2_errors a2_RT_KillSub(A2_interface *i, A2_handle voice)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	A2_event *e;
	A2_event **eq = a2_GetEventQueue(st, voice);
	if(!eq)
		return A2_BADVOICE;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	e->b.common.action = A2MT_KILLSUB;
	if(ii->flags & A2_TIMESTAMP)
		e->b.common.timestamp = ii->timestamp;
	else
		e->b.common.timestamp = st->now_fragstart;
	a2_SendEvent(eq, e);
	return A2_OK;
}


/*----- Common implementation ---------------------------*/

static A2_handle a2_common_NewGroup(A2_interface *i, A2_handle parent)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return a2_Starta(i, parent, st->ss->groupdriver, 0, NULL);
}


/*---------------------------------------------------------
	Adding and removing interfaces
---------------------------------------------------------*/

A2_interface_i *a2_AddInterface(A2_state *st, int flags)
{
	A2_interface_i *ii = (A2_interface_i *)calloc(1,
			sizeof(A2_interface_i));
	A2_interface *i = &ii->interface;
	if(!ii)
		return NULL;

	ii->state = st;
	ii->flags = flags;

	/* Set default timestamp jitter margin */
	ii->tsmargin = st->config->buffer * 1000 / st->config->samplerate;

	/* Grab the appropriate implementations! */
	if((ii->flags & A2_REALTIME) || !(st->config->flags & A2_REALTIME))
	{
		/* Direct calls into the engine */
		i->Release = a2_RT_Release;
		i->TimestampNow = a2_RT_TimestampNow;
		i->TimestampNudge = a2_RT_TimestampNudge;
		i->TimestampGet = a2_common_TimestampGet;
		i->TimestampSet = a2_common_TimestampSet;
		i->ms2Timestamp = a2_common_ms2Timestamp;
		i->Timestamp2ms = a2_common_Timestamp2ms;
		i->TimestampBump = a2_common_TimestampBump;
		i->NewGroup = a2_common_NewGroup;
		i->Starta = a2_RT_Starta;
		i->Playa = a2_RT_Playa;
		i->Senda = a2_RT_Senda;
		i->SendSuba = a2_RT_SendSuba;
		i->Kill = a2_RT_Kill;
		i->KillSub = a2_RT_KillSub;
	}
	else
	{
		/* Lock-free API/engine gateway */
		i->Release = a2_API_Release;
		i->TimestampNow = a2_API_TimestampNow;
		i->TimestampNudge = a2_API_TimestampNudge;
		i->TimestampGet = a2_common_TimestampGet;
		i->TimestampSet = a2_common_TimestampSet;
		i->ms2Timestamp = a2_common_ms2Timestamp;
		i->Timestamp2ms = a2_common_Timestamp2ms;
		i->TimestampBump = a2_common_TimestampBump;
		i->NewGroup = a2_common_NewGroup;
		i->Starta = a2_API_Starta;
		i->Playa = a2_API_Playa;
		i->Senda = a2_API_Senda;
		i->SendSuba = a2_API_SendSuba;
		i->Kill = a2_API_Kill;
		i->KillSub = a2_API_KillSub;
	}

	/* Add interface last in list */
	ii->next = NULL;
	if(st->interfaces)
	{
		A2_interface_i *j = st->interfaces;
		while(j->next)
			j = j->next;
		j->next = ii;
	}
	else
		st->interfaces = ii;
	ii->refcount = 1;

	return ii;
}


void a2_RemoveInterface(A2_interface_i *ii)
{
	if(ii->state)
	{
		A2_interface_i *j = ii->state->interfaces;
		if(ii == j)
			ii->state->interfaces = ii->next;
		else
		{
			while(j->next != ii)
				j = j->next;
			j->next = ii->next;
		}
	}
	free(ii);
}


/* Public API wrapper */
A2_interface *a2_Interface(A2_interface *master, int flags)
{
	A2_interface_i *ii = (A2_interface_i *)master;
	A2_state *st = ii->state;
	A2_interface_i *nii = a2_AddInterface(st, flags);
	if(!nii)
		return NULL;
	return &nii->interface;
}
