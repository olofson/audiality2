/*
 * api.c - Audiality 2 asynchronous API implementation
 *
 * Copyright 2010-2012 David Olofson <david@olofson.net>
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
#include "dsp.h"
#include "internals.h"
#include "compiler.h"


/*---------------------------------------------------------
	Handle management
---------------------------------------------------------*/

A2_handle a2_RootVoice(A2_state *st)
{
	if(!st->audio && !st->audio->Process)
		return -A2_NOTRUNNING;
	return st->rootvoice;
}


A2_otypes a2_TypeOf(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return -1;
	return (A2_otypes)hi->typecode;
}


const char *a2_TypeName(A2_state *st, A2_otypes type)
{
	if(!st->ss)
		return NULL;
	return rchm_TypeName(&st->ss->hm, type);
}


const char *a2_String(A2_state *st, A2_handle handle)
{
	char *sb;
	RCHM_handleinfo *hi;
	if(!st->ss)
		return NULL;
	sb = st->ss->strbuf;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	switch(hi->typecode)
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
		A2_unitdesc *ud = (A2_unitdesc *)hi->d.data;
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
	if(!st->ss)
		return NULL;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	switch(hi->typecode)
	{
	  case A2_TBANK:
		return ((A2_bank *)hi->d.data)->name;
	  case A2_TUNIT:
		return ((A2_unitdesc *)hi->d.data)->name;
	  case A2_TWAVE:
	  case A2_TPROGRAM:
	  case A2_TSTRING:
	  case A2_TVOICE:
		return NULL;
	}
	return NULL;
}


A2_errors a2_Retain(A2_state *st, A2_handle handle)
{
	if(!st->ss)
		return A2_NOTRUNNING;
	return rchm_Retain(&st->ss->hm, handle);
}


A2_errors a2_Release(A2_state *st, A2_handle handle)
{
	if(!st->ss)
		return A2_NOTRUNNING;
	return -rchm_Release(&st->ss->hm, handle);
}


/*---------------------------------------------------------
	Async API message gateway
---------------------------------------------------------*/

/*
 * WARNING:
 *	Nasty business going on here...! To save space and bandwidth in the
 *	lock-free FIFOs (which cannot be reallocated on the fly!), we're only
 *	sending over the part of A2_apimessage that we actually use, passing
 *	the actual message size via the 'size' field. (Which could BTW be sized
 *	down to one byte - but let's not get into unaligned structs as well...)
 *	   To make matters worse, we need to write each message with a single
 *	sfifo_Write() call, because the reader at the other would need some
 *	rather hairy logic to deal with incomplete messages.
 */

typedef struct A2_apimessage
{
	unsigned	size;	/* Actual size of message */
	A2_handle	target;	/* Target object*/
	A2_eventbody	b;	/* Event body, as carried by A2_event */
} A2_apimessage;

/* Size of message up until and including field 'x' */
#define	A2_MSIZE(x)	(offsetof(A2_apimessage, x) + \
		sizeof(((A2_apimessage *)NULL)->x))

/* Minimum message size - we always read this number of bytes first! */
#define	A2_APIREADSIZE	(A2_MSIZE(b.action))


/* Set the size field of 'm' to 'size', and write it to 'f'. */
static inline A2_errors a2_writemsg(SFIFO *f, A2_apimessage *m, unsigned size)
{
#ifdef DEBUG
	if(size < A2_APIREADSIZE)
		fprintf(stderr, "WARNING: Too small message in a2_writemsg()! "
				"%d bytes (min: %d)\n", size, A2_APIREADSIZE);
#endif
	if(sfifo_Space(f) < size)
		return A2_OVERFLOW;
	m->size = size;
	m->b.argc = 0;
	if(sfifo_Write(f, m, size) != size)
		return A2_INTERNAL + 21;
	return A2_OK;
}

/*
 * Copy arguments into 'm', setting the argument count and size of the message,
 * and then write it to 'f'.
 */
static inline A2_errors a2_writemsgargs(SFIFO *f, A2_apimessage *m,
		unsigned argc, int *argv)
{
	unsigned argsize = sizeof(int) * argc;
	unsigned size = offsetof(A2_apimessage, b.a) + argsize;
	if(argc > A2_MAXARGS)
		return A2_MANYARGS;
	if(sfifo_Space(f) < size)
		return A2_OVERFLOW;
	m->size = size;
	m->b.argc = argc;
	memcpy(&m->b.a, argv, argsize);
	if(sfifo_Write(f, m, size) != size)
		return A2_INTERNAL + 22;
	return A2_OK;
}


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
		A2_event *e = st->sys->RTAlloc(st->sys, sizeof(A2_event));
		if(!e)
		{
			fprintf(stderr, "Audiality 2: Could not initialize "
					"internal event pool!\n");
			return A2_OOMEMORY;
		}
		e->next = st->eventpool;
		st->eventpool = e;
	}
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
	}
}


/*
 * Engine side message pump
 */

static inline void a2r_em_start(A2_state *st, A2_apimessage *am)
{
	A2_event *e;
	A2_voice *tv = a2_GetVoice(st, am->target);
	if(!(tv))
	{
		a2r_Error(st, A2_BADVOICE, "a2_em_start()[1]");
		return;
	}
	if(!(e = a2_AllocEvent(st)))
	{
		a2r_Error(st, A2_OOMEMORY, "a2_em_start()[2]");
		return;
	}
	e->b.action = am->b.action;
	/* Adjust late messages, to avoid "bad" timestamps internally! */
	if(a2_TSDiff(am->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				am->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2_em_start()[3]");
		am->b.timestamp = st->now_frames;
	}
	e->b.timestamp = am->b.timestamp;
	e->b.a1 = am->b.a1;	/* program handle */
	e->b.a2 = am->b.a2;	/* handle for new voice */
	if(am->size >= A2_MSIZE(b.argc))
	{
		e->b.argc = am->b.argc;
		memcpy(&e->b.a, &am->b.a, sizeof(int) * am->b.argc);
	}
	else
		e->b.argc = 0;
	MSGTRACK(e->source = "a2_em_start()";)
	a2_SendEvent(tv, e);
}

static inline void a2r_em_send(A2_state *st, A2_apimessage *am)
{
	A2_event *e;
	A2_voice *tv = a2_GetVoice(st, am->target);
	if(!(tv))
	{
		a2r_Error(st, A2_BADVOICE, "a2_em_send()[1]");
		return;
	}
	if(!(e = a2_AllocEvent(st)))
	{
		a2r_Error(st, A2_OOMEMORY, "a2_em_send()[2]");
		return;
	}
	e->b.action = am->b.action;
	if(a2_TSDiff(am->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				am->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2_em_send()[3]");
		am->b.timestamp = st->now_frames;
	}
	e->b.timestamp = am->b.timestamp;
	e->b.a1 = am->b.a1;	/* entry point index */
	if(am->size >= A2_MSIZE(b.argc))
	{
		e->b.argc = am->b.argc;
		memcpy(&e->b.a, &am->b.a, sizeof(int) * am->b.argc);
	}
	else
		e->b.argc = 0;
	MSGTRACK(e->source = "a2_em_send()";)
	a2_SendEvent(tv, e);
}

static inline void a2r_em_sendsub(A2_state *st, A2_apimessage *am)
{
	A2_voice *v = a2_GetVoice(st, am->target);
	if(!v)
	{
		a2r_Error(st, A2_BADVOICE, "a2_em_sendsub()[1]");
		return;
	}
	if(a2_TSDiff(am->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				am->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2_em_sendsub()[2]");
		am->b.timestamp = st->now_frames;
	}
	for(v = v->sub; v; v = v->next)
	{
		A2_event *e = a2_AllocEvent(st);
		if(!e)
		{
			a2r_Error(st, A2_OOMEMORY, "a2_em_sendsub()[3]");
			return;
		}
		e->b.action = am->b.action;
		e->b.timestamp = am->b.timestamp;
		e->b.a1 = am->b.a1;	/* entry point index */
		if(am->size >= A2_MSIZE(b.argc))
		{
			e->b.argc = am->b.argc;
			memcpy(&e->b.a, &am->b.a, sizeof(int) * am->b.argc);
		}
		else
			e->b.argc = 0;
		MSGTRACK(e->source = "a2_em_sendsub()";)
		a2_SendEvent(v, e);
	}
}

static inline void a2r_em_release(A2_state *st, A2_apimessage *am)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am->target);
	if(!hi)
		return;
	if(hi->typecode != A2_TVOICE)
		return;
	/* Tell the voice (if any!?) that it's been detached */
	if(hi->d.data)
	{
		A2_voice *v = (A2_voice *)hi->d.data;
		v->handle = -1;
		a2_VoiceDetach(v);
	}
	/*
	 * Respond back to the API: "Clear to free the handle!"
	 *
	 * NOTE:
	 *	We're reusing the message struct, so the handle we're sending
	 *	back is already in am->target!
	 */
	am->b.action = A2MT_DETACH;
	a2_writemsg(st->toapi, am, A2_MSIZE(b.action));
}

static inline void a2r_em_kill(A2_state *st, A2_apimessage *am)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am->target);
	if(!hi)
		return;
	if(hi->typecode != A2_TVOICE)
		return;
	if(!hi->d.data)
		return;
	a2_VoiceKill(st, (A2_voice *)hi->d.data);
	am->b.action = A2MT_DETACH;
	a2_writemsg(st->toapi, am, A2_MSIZE(b.action));
}

static inline void a2r_em_killsub(A2_state *st, A2_apimessage *am)
{
	A2_voice *v, *sv;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am->target);
	if(!hi)
		return;
	if(hi->typecode != A2_TVOICE)
		return;
	if(!hi->d.data)
		return;
	v = (A2_voice *)hi->d.data;
	for(sv = v->sub; sv; sv = sv->next)
		a2_VoiceKill(st, sv);
	memset(v->sv, 0, sizeof(v->sv));
}

A2_errors a2r_PumpEngineMessages(A2_state *st)
{
	while(sfifo_Used(st->fromapi) >= A2_APIREADSIZE)
	{
		A2_apimessage am;
		if(sfifo_Read(st->fromapi, &am, (unsigned)A2_APIREADSIZE) < 0)
		{
			fprintf(stderr, "Audiality 2: Engine side FIFO read"
					" error!\n");
			return A2_INTERNAL + 25;
		}
		if(am.size > A2_APIREADSIZE)
			if(sfifo_Read(st->fromapi,
					((char *)&am) + A2_APIREADSIZE,
					am.size - A2_APIREADSIZE) < 0)
			{
				fprintf(stderr, "Audiality 2: Engine side FIFO"
						" read error!\n");
				return A2_INTERNAL + 26;
			}
		switch(am.b.action)
		{
		  case A2MT_PLAY:
		  case A2MT_START:
			a2r_em_start(st, &am);
			break;
		  case A2MT_SEND:
			a2r_em_send(st, &am);
			break;
		  case A2MT_SENDSUB:
			a2r_em_sendsub(st, &am);
			break;
		  case A2MT_RELEASE:
			a2r_em_release(st, &am);
			break;
		  case A2MT_KILL:
			a2r_em_kill(st, &am);
			break;
		  case A2MT_KILLSUB:
			a2r_em_killsub(st, &am);
			break;
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unknown API message "
					"%d!\n", am.b.action);
			break;
#endif
		}
	}
	return A2_OK;
}


/*
 * API side message pump
 */

A2_errors a2_PumpAPIMessages(A2_state *st)
{
	while(sfifo_Used(st->toapi) >= A2_APIREADSIZE)
	{
		A2_apimessage am;
		if(sfifo_Read(st->toapi, &am, (unsigned)A2_APIREADSIZE) < 0)
		{
			fprintf(stderr, "Audiality 2: Engine side FIFO read"
					" error!\n");
			return A2_INTERNAL + 27;
		}
		if(am.size > A2_APIREADSIZE)
			if(sfifo_Read(st->toapi, (char *)&am + A2_APIREADSIZE,
					am.size - A2_APIREADSIZE) < 0)
			{
				fprintf(stderr, "Audiality 2: Engine side FIFO"
						" read error!\n");
				return A2_INTERNAL + 28;
			}
		if(am.size < A2_MSIZE(b.argc))
			am.b.argc = 0;
		switch(am.b.action)
		{
		  case A2MT_DETACH:
		  {
			RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am.target);
			if(!hi)
				break;
			if(hi->refcount)
				hi->typecode = A2_TDETACHED;
			else
				rchm_Free(&st->ss->hm, am.target);
			break;
		  }
		  case A2MT_ERROR:
		  {
			const char *s;
			memcpy(&s, &am.b.a2, sizeof(const char *));
			fprintf(stderr, "Audiality 2: [RT] %s (%s)\n",
					a2_ErrorString(am.b.a1), s);
			break;
		  }
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unknown engine message "
					"%d!\n", am.b.action);
			break;
#endif
		}
	}
	return A2_OK;
}


static inline void a2_poll_api(A2_state *st)
{
	if(sfifo_Used(st->toapi) >= A2_APIREADSIZE)
		a2_PumpAPIMessages(st);
}


/* Post realtime error message to the API */
A2_errors a2r_Error(A2_state *st, A2_errors e, const char *info)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_RTERRORS))
		return A2_OK;
	am.b.action = A2MT_ERROR;
	am.b.timestamp = st->now_ticks;
	am.b.a1 = e;
	/* NOTE: This invades a[0] on platforms with 64 bit pointers! */
	memcpy(&am.b.a2, &info, sizeof(const char *));
	return a2_writemsg(st->toapi, &am,
			offsetof(A2_apimessage, b.a2) + sizeof(const char *));
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
	am.b.action = A2MT_DETACH;
	am.target = h;
	a2_writemsg(st->toapi, &am, A2_MSIZE(b.action));
}


/*---------------------------------------------------------
	Utilities
---------------------------------------------------------*/

float a2_F2P(float f)
{
	return log2(f / A2_MIDDLEC);
}


float a2_Rand(A2_state *st, float max)
{
	return a2_Noise(&st->noisestate) * max / 65536.0f;
}


/*---------------------------------------------------------
	Playing and controlling
---------------------------------------------------------*/

void a2_Now(A2_state *st)
{
	unsigned nt, nf;
	int dt;
	a2_poll_api(st);
	do {
		nf = st->now_frames;
		nt = nf + (st->config->buffer << 8);
		dt = a2_GetTicks() - st->now_ticks;
	} while(nf != st->now_guard);
	if(dt < 0)
		dt = 0;	/* Audio has been off for a looooong time... */
	nt += (int64_t)st->msdur * dt >> 8;
	if(a2_TSDiff(nt, st->timestamp) >= 0)
		st->timestamp = nt;
}


void a2_Wait(A2_state *st, float dt)
{
	st->timestamp += (unsigned)(st->msdur * dt / 256.0f);
}


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
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = parent;
	am.b.action = A2MT_START;
	am.b.timestamp = st->timestamp;
	am.b.a1 = program;
	if((am.b.a2 = rchm_New(&st->ss->hm, NULL, A2_TVOICE)) < 0)
		return am.b.a2;
	if((res = a2_writemsgargs(st->fromapi, &am, argc, argv)))
		return res;
	return am.b.a2;
}


A2_errors a2_Playa(A2_state *st, A2_handle parent, A2_handle program,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = parent;
	am.b.action = A2MT_PLAY;
	am.b.timestamp = st->timestamp;
	am.b.a1 = program;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv);
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.a1));
}


A2_errors a2_Senda(A2_state *st, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if((ep < 0) || (ep >= A2_MAXEPS))
		return A2_INDEXRANGE;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_SEND;
	am.b.timestamp = st->timestamp;
	am.b.a1 = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv);
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.a1));
}


A2_errors a2_SendSuba(A2_state *st, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if((ep < 0) || (ep >= A2_MAXEPS))
		return A2_INDEXRANGE;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_SENDSUB;
	am.b.timestamp = st->timestamp;
	am.b.a1 = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv);
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.a1));
}


A2_errors a2_Kill(A2_state *st, A2_handle voice)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_KILL;
	am.b.timestamp = st->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.timestamp));
}


A2_errors a2_KillSub(A2_state *st, A2_handle voice)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_KILLSUB;
	am.b.timestamp = st->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.timestamp));
}


void a2_InstaKillAllVoices(A2_state *st)
{
	A2_voice *v, *sv;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, st->rootvoice);
	st->audio->Lock(st->audio);
	if(!hi || (hi->typecode != A2_TVOICE) || (!hi->d.data))
	{
		st->audio->Unlock(st->audio);
		return;
	}
	v = (A2_voice *)hi->d.data;
	for(sv = v->sub; sv; sv = sv->next)
		a2_VoiceKill(st, sv);
	memset(v->sv, 0, sizeof(v->sv));
	st->audio->Unlock(st->audio);
}


static RCHM_errors a2_VoiceDestructor(RCHM_handleinfo *hi, void *td, RCHM_handle h)
{
	A2_state *st = (A2_state *)td;
	A2_apimessage am;
	am.target = h;
	am.b.action = A2MT_RELEASE;
	a2_writemsg(st->fromapi, &am, A2_MSIZE(b.action));
	return RCHM_REFUSE;
}

A2_errors a2_RegisterAPITypes(A2_state *st)
{
	A2_errors res = rchm_RegisterType(&st->ss->hm, A2_TVOICE, "voice",
			a2_VoiceDestructor, st);
	if(!res)
		res = rchm_RegisterType(&st->ss->hm, A2_TDETACHED, "detached",
			NULL, st);
	return res;
}


/*---------------------------------------------------------
	Object property interface
---------------------------------------------------------*/

int a2_GetProperty(A2_state *st, A2_handle h, A2_properties p)
{
	int res;
	switch(p)
	{
	  case A2_PSAMPLERATE:
		return st->config->samplerate;
	  case A2_PBUFFER:
		return st->config->buffer;
	  case A2_PCHANNELS:
		return st->config->channels;
	  case A2_PACTIVEVOICES:
		return st->activevoices;
	  case A2_PFREEVOICES:
		return st->totalvoices - st->activevoices;
	  case A2_PTOTALVOICES:
		return st->totalvoices;
	  case A2_PCPULOADAVG:
		res = st->cpuloadavg;
		st->statreset = 1;
		return res;
	  case A2_PCPULOADMAX:
		return st->cpuloadmax;
	  case A2_PCPUTIMEAVG:
		res = st->cputimeavg;
		st->statreset = 1;
		return res;
	  case A2_PCPUTIMEMAX:
		return st->cputimemax;
	  case A2_PINSTRUCTIONS:
		return st->instructions;
	  case A2_PEXPORTALL:
		return st->ss->c->exportall;
	  default:
		return 0;
	}
}

A2_errors a2_SetProperty(A2_state *st, A2_handle h, A2_properties p, int v)
{
	switch(p)
	{
	  case A2_PCPULOADAVG:
	  case A2_PCPUTIMEAVG:
		st->statreset = 1;
		return A2_OK;
	  case A2_PCPULOADMAX:
		st->cpuloadmax = v;
		return A2_OK;
	  case A2_PCPUTIMEMAX:
		st->cputimemax = v;
		return A2_OK;
	  case A2_PINSTRUCTIONS:
		st->instructions = v;
		return A2_OK;
	  case A2_PEXPORTALL:
		st->ss->c->exportall = v;
		return A2_OK;
	  default:
		return A2_NOTFOUND;
	}
}
