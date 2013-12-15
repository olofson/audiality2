/*
 * core.c - Audiality 2 realtime core and scripting VM
 *
 * Copyright 2010-2013 David Olofson <david@olofson.net>
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
#include <math.h>
#include "internals.h"
#include "dsp.h"
#include "inline.h"


/*---------------------------------------------------------
	Voice and audio processing
---------------------------------------------------------*/

static inline A2_errors a2_VoicePush(A2_state *st, A2_voice *v, int firstreg,
		int interrupt)
{
	A2_stackentry *se = &a2_AllocBlock(st)->stackentry;
	if(!se)
		return A2_OOMEMORY;
	se->prev = v->stack;
	v->stack = se;
	se->state = v->s.state;
	se->func = v->s.func;
	se->pc = v->s.pc;
	se->interrupt = interrupt;
	se->timer = v->s.timer;
	se->firstreg = firstreg;
	memcpy(se->r, v->s.r + firstreg, sizeof(int) * (A2_REGISTERS - firstreg));
	return A2_OK;
}


static inline int a2_VoicePop(A2_state *st, A2_voice *v)
{
	A2_stackentry *se = v->stack;
	int inter = se->interrupt;
	v->s.state = se->state;
	v->s.func = se->func;
	if(inter)
	{
		v->s.pc = se->pc;
		v->s.timer = se->timer;
	}
	else
		v->s.pc = se->pc + 1;
	memcpy(v->s.r + se->firstreg, se->r,
			sizeof(int) * (A2_REGISTERS - se->firstreg));
	v->stack = se->prev;
	a2_FreeBlock(st, se);
	return inter;
}


static inline void a2_VoiceControl(A2_state *st, A2_voice *v, unsigned reg,
		unsigned start, unsigned duration)
{
	A2_write_cb cb = v->cwrite[reg];
	if(cb)
		cb(v->cunit[reg], v->s.r[reg], start, duration);
}


/*
 * Instantiatiate, initialize and wire a unit as described by descriptor 'ud',
 * and add it at the end of the chain in voice 'v'.
 *
 * NOTE:
 *	We're not keeping a pointer to the last unit in A2_voice (voice
 *	structures are "hardwired" by the compiler anyway, so they're never
 *	rebuilt on the fly), we keep it as a temporary variable when
 *	constructing the voice, and pass it via the 'lastunit' argument instead.
 */
static inline A2_unit *a2_AddUnit(A2_state *st, const A2_structitem *si,
		A2_voice *v, A2_unit *lastunit, int32_t **scratch,
		unsigned noutputs, int32_t **outputs)
{
	unsigned i;
	A2_errors res;
	int minoutputs, maxoutputs, ninputs;
	const A2_unitdesc *ud = si->unitdesc;
	A2_unit *u = &a2_AllocBlock(st)->unit;
	if(!u)
		return NULL;

	DUMPSTRUCTRT(fprintf(stderr, "Wiring %s... ", ud->name);)

	/* Input wiring */
	switch(si->ninputs)
	{
	  case A2_IO_MATCHOUT:
		ninputs = noutputs;
		if(ninputs < ud->mininputs)
		{
			a2_FreeBlock(st, u);
			DBG(fprintf(stderr, "Audiality 2: Voice %p has too few "
					"channels for unit '%s'!\n",
					v, ud->name);)
			a2r_Error(st, A2_FEWCHANNELS, "a2_AddUnit()");
			return NULL;
		}
		else if(ninputs > ud->maxinputs)
			ninputs = ud->maxinputs;
		break;
	  default:
		ninputs = si->ninputs;
		break;
	}

	/* Some units require ninputs == noutputs... */
	if(ud->flags & A2_MATCHIO)
		minoutputs = maxoutputs = ninputs;
	else
	{
		minoutputs = ud->minoutputs;
		maxoutputs = ud->maxoutputs;
	}

	/* Output wiring */
	switch(si->noutputs)
	{
	  case A2_IO_WIREOUT:
	  case A2_IO_MATCHOUT:
		u->noutputs = noutputs;
		if(u->noutputs < minoutputs)
		{
			a2_FreeBlock(st, u);
			DBG(fprintf(stderr, "Audiality 2: Voice %p has too few "
					"channels for unit '%s'!\n",
					v, ud->name);)
			a2r_Error(st, A2_FEWCHANNELS, "a2_AddUnit()");
			return NULL;
		}
		else if(u->noutputs > maxoutputs)
			u->noutputs = maxoutputs;
		break;
	  default:
		u->noutputs = si->noutputs;
		break;
	}
	if(si->noutputs == A2_IO_WIREOUT)
		u->outputs = outputs;
	else
		u->outputs = scratch;

	/* Initialize instance struct and wire any control registers */
	u->descriptor = ud;
	u->registers = v->s.r + v->cregisters;
	for(i = 0; ud->registers[i].name; ++i)
	{
		v->cwrite[v->cregisters] = ud->registers[i].write;
		v->cunit[v->cregisters] = u;
		++v->cregisters;
	}
	u->ninputs = ninputs;
	u->inputs = scratch;
	DUMPSTRUCTRT(fprintf(stderr, "in: %d\tout:%d", u->ninputs, u->noutputs);)

	if((ud->flags & A2_MATCHIO) && (u->ninputs != u->noutputs))
	{
		a2_FreeBlock(st, u);
		DBG(fprintf(stderr, "Audiality 2: Unit '%s' needs to have "
				"matching input/output counts!\n", ud->name);)
		a2r_Error(st, A2_IODONTMATCH, "a2_AddUnit()");
		return NULL;
	}

	/* Initialize the unit instance itself! */
	if((res = ud->Initialize(u, &v->s, st->config, si->flags)))
	{
		a2_FreeBlock(st, u);
		DBG(fprintf(stderr, "Audiality 2: Unit '%s' on voice %p failed to "
				"initialize! (%s)\n",
				ud->name, v, a2_ErrorString(res));)
		a2r_Error(st, res, "a2_AddUnit()");
		return NULL;
	}
	DUMPSTRUCTRT(fprintf(stderr, "\n");)

	/* Add to list! */
	if(lastunit)
		lastunit->next = u;
	else
		v->units = u;
	u->next = NULL;

	return u;
}


/*
 * Deinitialize and destroy unit 'u'.
 *
 * NOTE:
 *	This does NOT remove the unit from the voice unit chain! The unit must
 *	be detached from the list before destroyed with this function.
 */
static inline void a2_DestroyUnit(A2_state *st, A2_unit *u)
{
	if(u->descriptor->Deinitialize)
		u->descriptor->Deinitialize(u, st);
	a2_FreeBlock(st, u);
}


/*
 * Populate voice 'v' with units as described by program 'p'.
 */
static inline A2_errors a2_PopulateVoice(A2_state *st, const A2_program *p,
		A2_voice *v)
{
	A2_structitem *si;
	A2_unit *lastu = NULL;
	int32_t **scratch;

	/* The 'inline' unit changes these! */
	unsigned noutputs = v->noutputs;
	int32_t **outputs = v->outputs;

	if(!p->structure)
		return A2_OK;	/* No units - all done! */

	/* Make sure we have enough scratch buffers, if any are needed */
	if(p->buffers)
	{
		A2_bus **b = st->scratch + v->nestlevel;
		int bmin = p->buffers;
		if(bmin < 0)
		{
			/*
			 * We have units using scratch buffers while adapting
			 * to the voice output channel count! Make sure we have
			 * enough buffers to match the bus, to safely handle
			 * autowiring.
			 */
			bmin = -bmin;
			if(bmin < noutputs)
				bmin = noutputs;
		}
		DUMPSTRUCTRT(fprintf(stderr, "%sllocating %d channel bus for "
				"voice %p, nestlevel %d\n", *b ? "Rea" : "A",
		      		bmin, v, v->nestlevel);)
		if(!*b)
		{
			if(!(*b = a2_AllocBus(st, bmin)))
				return A2_OOMEMORY;
		}
		else if((*b)->channels < bmin)
		{
			if(!a2_ReallocBus(st, *b, bmin))
				return A2_OOMEMORY;
		}
		scratch = (*b)->buffers;
	}

	/* Add and wire the voice units! */
	for(si = p->structure; si; si = si->next)
		if(!(lastu = a2_AddUnit(st, si, v, lastu, scratch,
				noutputs, outputs)))
			return A2_VOICEINIT;

	return A2_OK;
}


/*============================================================================
 * WARNING: These are tuned for minimal init/cleanup overhead! Be careful...
 *============================================================================*/

A2_voice *a2_VoiceAlloc(A2_state *st)
{
	A2_voice *v = (A2_voice *)st->sys->RTAlloc(st->sys, sizeof(A2_voice));
	if(!v)
	{
		a2r_Error(st, A2_OOMEMORY, "a2_VoiceAlloc()");
		return NULL;
	}
	v->sub = NULL;
	v->stack = NULL;
	v->program = NULL;
	v->events = NULL;
	v->units = NULL;
	v->cregisters = A2_FIXEDREGS;	/* Start at the first "free" register */
	v->handle = -1;
	memset(v->sv, 0, sizeof(v->sv));
	memset(v->cwrite, 0, sizeof(v->cwrite));
	++st->totalvoices;
#ifdef DEBUG
	if(st->audio && st->audio->Process && (st->config->flags & A2_REALTIME))
		fprintf(stderr, "Audiality 2: Voice pool exhausted! "
				"Allocated new voice %p.\n", v);
#endif
	return v;
}


A2_voice *a2_VoiceNew(A2_state *st, A2_voice *parent)
{
	A2_voice *v = st->voicepool;
	if(parent->nestlevel >= A2_NESTLIMIT - 1)
	{
		/* FIXME: Can we get the program name here instead? */
		a2r_Error(st, A2_VOICENEST, "a2_VoiceNew()");
		return NULL;
	}
	if(v)
		st->voicepool = v->next;
	else if(!(v = a2_VoiceAlloc(st)))
		return NULL;
	++st->activevoices;
	v->nestlevel = parent->nestlevel + 1;
	v->next = parent->sub;
	parent->sub = v;
	v->s.timer = 0;
	v->s.r[R_TICK] = parent->s.r[R_TICK];
	v->s.r[R_TRANSPOSE] = parent->s.r[R_TRANSPOSE];
	v->noutputs = parent->noutputs;
	v->outputs = parent->outputs;
	return v;
}


/* Special constructor for the root voice, which doesn't have a parent. */
A2_errors a2_init_root_voice(A2_state *st)
{
	int i, res;
	A2_voice *v;
	A2_program *rootdriver;
	/*
	 * FIXME: We can't handle arbitrary channel counts very well at this
	 * FIXME: point, so we actually mix stereo internally at all times.
	 */
	const char *rd = "a2_rootdriver";
	if(st->config->channels < 2)
		rd = "a2_rootdriver_mono";
	rootdriver = a2_GetProgram(st, a2_Get(st, A2_ROOTBANK, rd));
	if(!rootdriver)
		return A2_INTERNAL + 400;
	if(!(v = a2_VoiceAlloc(st)))
		return A2_OOMEMORY;
	st->rootvoice = rchm_NewEx(&st->ss->hm, v, A2_TVOICE, A2_LOCKED, 1);
	if(st->rootvoice < 0)
		return -st->rootvoice;
	v->handle = st->rootvoice;
	++st->activevoices;
	v->nestlevel = 0;
	v->flags = A2_ATTACHED;
	v->s.timer = 0;
	v->next = NULL;
	v->s.r[R_TICK] = A2_DEFAULTTICK;
	v->s.r[R_TRANSPOSE] = 0;
	v->noutputs = st->master->channels;
	v->outputs = st->master->buffers;
	for(i = A2_FIRSTCONTROLREG; i < v->cregisters; ++i)
		a2_VoiceControl(st, v, i, 0, 0);
	if((res = a2_VoiceStart(st, v, rootdriver, 0, NULL)))
	{
		a2_VoiceFree(st, &v);
		rchm_Free(&st->ss->hm, st->rootvoice);
		return res;
	}
	return A2_OK;
}


void a2_VoiceFree(A2_state *st, A2_voice **head)
{
	unsigned r;
	A2_voice *v = *head;
	a2_VoiceKill(st, v);
	*head = v->next;
	v->next = st->voicepool;
	st->voicepool = v;
	--st->activevoices;
	v->program = NULL;
	for(r = A2_FIXEDREGS; r < v->cregisters; ++r)
		v->cwrite[r] = NULL;
	v->cregisters = A2_FIXEDREGS;
	while(v->events)
	{
		A2_event *e = v->events;
		v->events = e->next;
		a2_FreeEvent(st, e);
	}
	while(v->units)
	{
		A2_unit *u = v->units;
		v->units = u->next;
		a2_DestroyUnit(st, u);
	}
#ifdef DEBUG
	assert(!v->sub);
	assert(!v->stack);
#endif
}


void a2_VoiceKill(A2_state *st, A2_voice *v)
{
	while(v->stack)
		a2_VoicePop(st, v);
	v->program = st->ss->terminator;
	v->s.func = 0;
	v->s.pc = 0;
	v->s.timer = 0;
	v->s.state = A2_RUNNING;
	v->flags &= ~A2_ATTACHED;
	if(v->handle >= 0)
	{
		a2r_DetachHandle(st, v->handle);
		v->handle = -1;
	}
	while(v->sub)
		a2_VoiceFree(st, &v->sub);
	memset(v->sv, 0, sizeof(v->sv));
}


/*============================================================================
 * /WARNING
 *============================================================================*/


/*
 * Start program 'p' on voice 'v'.
 *
 * NOTE:
 *	As of 1.9.1, voices are NOT populated at this point! This is instead
 *	performed by the INITV VM instruction, in order to have the voice units
 *	initialized and started at the exact start time of the program.
 */
A2_errors a2_VoiceStart(A2_state *st, A2_voice *v,
		A2_program *p, int argc, int *argv)
{
	int i;
	v->program = p;
	v->flags |= p->vflags;	/* A2_SUBINLINE etc */
	v->s.func = 0;
	v->s.pc = 0;
	v->s.state = A2_RUNNING;

	/* Grab the arguments! */
	if(argc > p->funcs[0].argc)
		argc = p->funcs[0].argc;
	memcpy(v->s.r + p->funcs[0].argv, argv, argc * sizeof(int));

	/* Get the defaults for any unspecified arguments */
	for(i = argc; i < p->funcs[0].argc; ++i)
		v->s.r[i + p->funcs[0].argv] = p->funcs[0].argdefs[i];

	/* Unit control registers start after the main program arguments! */
	v->cregisters = p->funcs->argv + p->funcs->argc;

	return A2_OK;
}


/*
 * Make a function or interrupt call to function 'func' of the current program.
 */
A2_errors a2_VoiceCall(A2_state *st, A2_voice *v, unsigned func,
		int argc, int *argv, int interrupt)
{
	int i;
	A2_function *fn = v->program->funcs + func;
	if(a2_VoicePush(st, v, fn->argv, interrupt))
		return A2_OOMEMORY;
	v->s.func = func;
	v->s.pc = 0;
	if(interrupt)
		v->s.state = A2_INTERRUPT;
	if(argc > fn->argc)
		argc = fn->argc;
	memcpy(v->s.r + fn->argv, argv, argc * sizeof(int));
	for(i = argc; i < fn->argc; ++i)
		v->s.r[i + fn->argv] = fn->argdefs[i];
	return A2_OK;
}


/* Enqueue a timestamped event for entry point 'ep' of the specified voice. */
static inline A2_errors a2_VoiceSend(A2_state *st, A2_voice *v, unsigned when,
		unsigned ep, int argc, int *argv)
{
	A2_event *e;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	MSGTRACK(e->source = "a2_VoiceSend()";)
	e->b.action = A2MT_SEND;
	e->b.timestamp = when + st->now_fragstart;
	e->b.a1 = ep;
	e->b.argc = argc;
	memcpy(e->b.a, argv, argc * sizeof(int));
	a2_SendEvent(v, e);
	return A2_OK;
}


/* Start a new voice, at the specified time, with the specified program. */
static A2_errors a2_VoiceSpawn(A2_state *st, A2_voice *v, unsigned when,
		int vid, A2_handle program, int argc, int *argv)
{
	int res;
	A2_voice *nv;
	A2_program *p = a2_GetProgram(st, program);
	if((vid > 0) && (v->sv[vid]))	/* NOTE: No detach for id 0! */
		a2_VoiceDetach(v->sv[vid]);
	if(!p)
		return A2_BADPROGRAM;
	if(!(nv = a2_VoiceNew(st, v)))
		return v->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	if(vid > 0)
	{
		v->sv[vid] = nv;
		nv->flags = A2_ATTACHED;
	}
	else
		nv->flags = 0;
	if(!nv)
		return v->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	nv->s.timer = when;
	if((res = a2_VoiceStart(st, nv, p, argc, argv)))
		a2_VoiceFree(st, &v->sub);
	return res;
}


DUMPMSGS(static void printargs(int argc, int *argv)
{
	int i;
	for(i = 0; i < argc; ++i)
		if(i < argc - 1)
			fprintf(stderr, "%f, ", argv[i] / 65536.0f);
		else
			fprintf(stderr, "%f", argv[i] / 65536.0f);
})

/*
 * Start a new voice as specified by 'eb' under 'parent', and if specified,
 * attach it to 'handle', which would typically by pointed at the d.data
 * pointer in the RCHM_handleinfo struct.
 */
static inline A2_errors a2_event_start(A2_state *st, A2_voice *parent,
		A2_eventbody *eb, A2_voice **handle)
{
	A2_voice *v;
	A2_program *p = a2_GetProgram(st, eb->a1);
	if(!p)
		return A2_BADPROGRAM;
	if(!(v = a2_VoiceNew(st, parent)))
		return parent->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	if(handle)
	{
		*handle = v;
		v->flags = A2_ATTACHED;
	}
	else
		v->flags = 0;
	v->s.timer = eb->timestamp - st->now_fragstart;
	return a2_VoiceStart(st, v, p, eb->argc, eb->a);
}

/*
 * Process events. Will return when:
 *	* VM needs to run to respond to message. (Returns A2_OK)
 *	* Time/audio needs to advance. (Returns A2_END)
 *	* There are no more events in the queue. (Returns A2_END)
 *	* There is an error. (Returns an error code)
 *
TODO: Forward events where applicable, to save a realloc + copy.
 */
static A2_errors a2_VoiceProcessEvents(A2_state *st, A2_voice *v)
{
	unsigned current = v->events->b.timestamp >> 8;
	while(v->events)
	{
		int res;
		A2_event *e = v->events;
		if((e->b.timestamp >> 8) != current)
			return A2_END;
		DUMPMSGS(fprintf(stderr, "%f:\th (%p) ", e->b.timestamp / 256.0f, v);)
		NUMMSGS(fprintf(stderr, "[ %u ] ", e->number);)
#ifdef DEBUG
		if(a2_TSDiff(e->b.timestamp, st->now_fragstart) < 0)
		{
			fprintf(stderr, "Audiality 2: Incorrect timestamp for voice %p!"
					" (%f frames late.)", v,
			       		(st->now_fragstart - e->b.timestamp) / 256.0f);
			MSGTRACK(fprintf(stderr, "(ev %p from %s)", e, e->source);)
			fprintf(stderr, "\n");
			e->b.timestamp = st->now_fragstart;
		}
#endif
		switch(e->b.action)
		{
		  case A2MT_PLAY:
			DUMPMSGS(
				fprintf(stderr, "PLAY(");
				printargs(e->b.argc, e->b.a);
				fprintf(stderr, ")\n");
			)
			a2_event_start(st, v, &e->b, NULL);
			break;
		  case A2MT_START:
		  {
			RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, e->b.a2);
			DUMPMSGS(
				fprintf(stderr, "START(");
				printargs(e->b.argc, e->b.a);
				fprintf(stderr, ")\n");
			)
			a2_event_start(st, v, &e->b, (A2_voice **)(&hi->d.data));
			break;
		  }
		  case A2MT_SEND:
		  {
			int ep;
			DUMPMSGS(
				fprintf(stderr, "SEND(%u: ", e->b.a1);
				printargs(e->b.argc, e->b.a);
				fprintf(stderr, ")\n");
			)
			if((ep = v->program->eps[e->b.a1]) < 0)
			{
				a2r_Error(st, A2_BADENTRY,
						"a2_VoiceProcessEvents()[1]");
				break;
			}
			if((res = a2_VoiceCall(st, v, ep, e->b.argc, e->b.a, 1)))
			{
				a2r_Error(st, res, "a2_VoiceProcessEvents()[2]");
				break;
			}
			v->s.timer = e->b.timestamp & 0xff;
			v->events = e->next;
			a2_FreeEvent(st, e);
			return res;	/* Spin the VM to process the message! */
		  }
		  case A2MT_TAPCB:
		  {
			void **d = (void **)&e->b.a1;
			if((res = a2_set_xinsert_cb(st, v, d[0], d[1], 0)))
				a2r_Error(st, res, "a2_VoiceProcessEvents()[3]");
			break;
		  }
		  case A2MT_INSERTCB:
		  {
			void **d = (void **)&e->b.a1;
			if((res = a2_set_xinsert_cb(st, v, d[0], d[1], 1)))
				a2r_Error(st, res, "a2_VoiceProcessEvents()[4]");
			break;
		  }
		}
		v->events = e->next;
		a2_FreeEvent(st, e);
	}
	return A2_END;
}


/*
 * Register write tracker
 */
typedef struct A2_regtracker
{
	uint32_t	mask;			/* One bit/register */
	uint32_t	position;		/* Current index in regs[] */
	uint8_t		regs[A2_REGISTERS];	/* Indices of written regs */
} A2_regtracker;

static inline void a2_RTInit(A2_regtracker *rt)
{
	rt->mask = rt->position = 0;
}

static inline void a2_RTMark(A2_regtracker *rt, unsigned r)
{
	uint32_t b = 1 << r;
	if(b & rt->mask)
		return;		/* Already marked! --> */
	rt->mask |= b;
	rt->regs[rt->position++] = r;
}

static inline void a2_RTUnmark(A2_regtracker *rt, unsigned r)
{
	uint32_t b = 1 << r;
	if(b & rt->mask)
	{
		int i;
		rt->mask &= ~b;
		for(i = 0; i < rt->position; ++i)
			if(rt->regs[i] == r)
			{
				rt->regs[i] = rt->regs[--rt->position];
				break;
			}
	}
}

static inline void a2_RTApply(A2_regtracker *rt, A2_state *st, A2_voice *v,
		unsigned start, unsigned duration)
{
	int i;
	for(i = 0; i < rt->position; ++i)
		a2_VoiceControl(st, v, rt->regs[i], start, duration);
}


/* Convert musical tick duration to audio frame delta time */
static inline unsigned a2_VoiceTicks2t(A2_state *st, A2_voice *v, int d)
{
	return ((((uint64_t)d * v->s.r[R_TICK] + 127) >> 8) *
			st->msdur + 0x7fffffff) >> 32;
}


/* Get the size (number of elements) of an indexable object */
static int a2_sizeof_object(A2_state *st, int handle)
{
	A2_wave *w;
	if(handle < 0)
		return -A2_INVALIDHANDLE << 16;
	if(!(w = a2_GetWave(st, handle)))
		return -A2_WRONGTYPE << 16;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		break;
	  default:
		return -A2_WRONGTYPE << 16;
	}
	return ((int64_t)(w->d.wave.size[0]) << 16) / w->period;
}

/*
 * Execute VM instructions until a timing instruction is executed, or the
 * program ends. Returns A2_OK as long as the VM program wants to keep running.
 */
#define	A2_VMABORT(e, m)					\
	{							\
		st->instructions += A2_INSLIMIT - inscount;	\
		a2r_Error(st, e, m);				\
		return e;					\
	}
static inline A2_errors a2_VoiceVMProcess(A2_state *st, A2_voice *v, unsigned frame)
{
	int res;
	int cargc = 0, cargv[A2_MAXARGS];	/* run/spawn argument stack */
	unsigned *code = v->program->funcs[v->s.func].code;
	int *r = v->s.r;
	unsigned inscount = A2_INSLIMIT;
	A2_regtracker rt;
	if(v->s.timer >= 256)
		return A2_OK;
	if(v->s.state == A2_WAITING)
		v->s.state = A2_RUNNING;
	a2_RTInit(&rt);
	while(1)
	{
		unsigned dt;
		A2_instruction *ins = (A2_instruction *)(code + v->s.pc);
		DUMPCODERT(
			fprintf(stderr, "%p: ", v);
			a2_DumpIns(code, v->s.pc);
		)
		if(!--inscount)
		{
			a2_VoiceKill(st, v);
			A2_VMABORT(A2_OVERLOAD, "VM");
		}
		switch(ins->opcode)
		{

		/* Program flow control */
		  case OP_END:
			a2_RTApply(&rt, st, v, 255 - v->s.timer, 0);
			v->s.timer = 0xffffffff;
			if(v->s.state == A2_FINALIZING)
			{
				/* Wait for subvoices to terminate */
				st->instructions += A2_INSLIMIT - inscount;
				return v->sub ? A2_OK : A2_END;
			}
			v->s.state = A2_ENDING;
			if((v->flags & A2_ATTACHED) || v->events)
			{
				/* Hang around until detached! */
				st->instructions += A2_INSLIMIT - inscount;
				return A2_OK;
			}
			v->s.state = A2_FINALIZING;
			if(!v->sub)
			{
				/* That's it - all done! */
				st->instructions += A2_INSLIMIT - inscount;
				return A2_END;
			}
			/* Detach subvoices, then wait for them to terminate */
			memset(v->sv, 0, sizeof(v->sv));
			for(v = v->sub; v; v = v->next)
				a2_VoiceDetach(v);
			st->instructions += A2_INSLIMIT - inscount;
			return A2_OK;
		  case OP_RETURN:
		  {
			unsigned now = v->s.timer;
			if(a2_VoicePop(st, v))
			{
				/* Return from interrupt */
				code = v->program->funcs[v->s.func].code;
				if(v->s.state >= A2_ENDING)
					continue;
				dt = v->s.timer;
				v->s.timer = now;
				goto timing_interrupt;
			}
			else
			{
				/* Return from local function */
				code = v->program->funcs[v->s.func].code;
				continue;
			}
		  }
		  case OP_CALL:
			DBG(if(!ins->a2)
				fprintf(stderr, "Tried to CALL function 0!\n");)
			DBG(if(ins->a2 >= v->program->nfuncs)
				fprintf(stderr, "Function index %d out of range!\n", ins->a2);)
			if((res = a2_VoiceCall(st, v, ins->a2, cargc, cargv, 0)))
				A2_VMABORT(res, "VM:CALL");
			code = v->program->funcs[v->s.func].code;
			cargc = 0;
			continue;

		/* Local flow control */
		  case OP_JUMP:
			v->s.pc = ins->a2;
			continue;
		  case OP_LOOP:
			r[ins->a1] -= 65536;
			if(r[ins->a1] <= 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JZ:
			if(r[ins->a1])
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JNZ:
			if(!r[ins->a1])
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JG:
			if(r[ins->a1] <= 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JL:
			if(r[ins->a1] >= 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JGE:
			if(r[ins->a1] < 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JLE:
			if(r[ins->a1] > 0)
				break;
			v->s.pc = ins->a2;
			continue;

		/* Timing */
		  case OP_DELAY:
			dt = ((int64_t)ins->a3 * st->msdur + 0x7fffff) >> 24;
			++v->s.pc;
			goto timing;
		  case OP_DELAYR:
			dt = ((int64_t)r[ins->a1] * st->msdur + 0x7fffff) >> 24;
			goto timing;
		  case OP_TDELAY:
			dt = a2_VoiceTicks2t(st, v, ins->a3);
			++v->s.pc;
			goto timing;
		  case OP_TDELAYR:
			dt = a2_VoiceTicks2t(st, v, r[ins->a1]);
			goto timing;

		/* Arithmetics */
		  case OP_SUBR:
			r[ins->a1] -= r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_DIVR:
			if(!r[ins->a2])
				A2_VMABORT(A2_DIVBYZERO, "VM:DIVR");
			r[ins->a1] = ((int64_t)r[ins->a1] << 16) / r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_P2DR:
			r[ins->a1] = 65536000.0f / (powf(2.0f, r[ins->a2] *
					(1.0f / 65536.0f)) * A2_MIDDLEC) + 0.5f;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_NEGR:
			r[ins->a1] = -r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_LOAD:
			r[ins->a1] = ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_LOADR:
			r[ins->a1] = r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_ADD:
			r[ins->a1] += ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_ADDR:
			r[ins->a1] += r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_MUL:
			r[ins->a1] = (int64_t)r[ins->a1] * ins->a3 >> 16;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_MULR:
			r[ins->a1] = (int64_t)r[ins->a1] * r[ins->a2] >> 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_MOD:
			r[ins->a1] %= ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_MODR:
			if(!r[ins->a2])
				A2_VMABORT(A2_DIVBYZERO, "VM:MODR");
			r[ins->a1] %= r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_QUANT:
			r[ins->a1] = r[ins->a1] / ins->a3 * ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_QUANTR:
			if(!r[ins->a2])
				A2_VMABORT(A2_DIVBYZERO, "VM:QUANTR");
			r[ins->a1] = r[ins->a1] / r[ins->a2] * r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_RAND:
			r[ins->a1] = (int64_t)a2_Noise(&st->noisestate) *
					ins->a3 >> 16;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_RANDR:
			r[ins->a1] = (int64_t)a2_Noise(&st->noisestate) *
					r[ins->a2] >> 16;
			a2_RTMark(&rt, ins->a1);
			break;

		/* Comparison operators */
/*TODO: Versions with an immediate second operand! */
		  case OP_GR:
			r[ins->a1] = (r[ins->a1] > r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_LR:
			r[ins->a1] = (r[ins->a1] < r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_GER:
			r[ins->a1] = (r[ins->a1] >= r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_LER:
			r[ins->a1] = (r[ins->a1] <= r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_EQR:
			r[ins->a1] = (r[ins->a1] == r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_NER:
			r[ins->a1] = (r[ins->a1] != r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;

		/* Boolean operators */
		  case OP_ANDR:
			r[ins->a1] = (r[ins->a1] && r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_ORR:
			r[ins->a1] = (r[ins->a1] || r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_XORR:
			r[ins->a1] = (!r[ins->a1] != !r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_NOTR:
			r[ins->a1] = (!r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;

		/* Unit control */
		  case OP_SET:
			a2_VoiceControl(st, v, ins->a1, 255 - v->s.timer, 0);
			a2_RTUnmark(&rt, ins->a1);
			break;
#if 0
		  case OP_RAMP:
			++v->s.pc;
			if(ins->a1 >= v->cregisters)
				break;
			a2_VoiceControl(v, ins->a1,
					(int64_t)ins->a3 * st->msdur >> 32);
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_RAMPR:
			if(ins->a1 >= v->cregisters)
				break;
			a2_VoiceControl(v, ins->a1,
					(int64_t)r[ins->a2] * st->msdur >> 32);
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_DETACHR:
		  {
			unsigned vid = r[ins->a1] >> 16;
			if(vid > A2_REGISTERS)
			{
				st->instructions += (A2_INSLIMIT - inscount);
				return A2_BADVOICE;
			}
			if(v->sv[vid])
				a2_VoiceDetach(v->sv[vid]);
			v->sv[vid] = NULL;
			break;
		  }
		  case OP_DETACH:
			if(v->sv[ins->a1])
				a2_VoiceDetach(v->sv[ins->a1]);
			v->sv[ins->a1] = NULL;
			break;
#endif

		/* Subvoice control */
		  case OP_PUSH:
			if(cargc >= A2_MAXARGS)
				A2_VMABORT(A2_MANYARGS, "VM:PUSH");
			cargv[cargc++] = ins->a3;
			++v->s.pc;
			break;
		  case OP_PUSHR:
			if(cargc >= A2_MAXARGS)
				A2_VMABORT(A2_MANYARGS, "VM:PUSHR");
			cargv[cargc++] = r[ins->a1];
			break;
		  case OP_SPAWNVR:
		  {
			unsigned vid = r[ins->a1] >> 16;
			unsigned when = v->s.timer;
			if(!(v->flags & A2_SUBINLINE))
				when += frame << 8;
			if(vid >= A2_REGISTERS)
				A2_VMABORT(A2_BADVOICE, "VM:SPAWNRR");
			a2_VoiceSpawn(st, v, when, vid, r[ins->a2] >> 16,
					cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SPAWNV:
		  {
			unsigned vid = r[ins->a1] >> 16;
			unsigned when = v->s.timer;
			if(!(v->flags & A2_SUBINLINE))
				when += frame << 8;
			if(vid >= A2_REGISTERS)
				A2_VMABORT(A2_BADVOICE, "VM:SPAWNRR");
			a2_VoiceSpawn(st, v, when, vid, ins->a2,
					cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SPAWNR:
		  {
			unsigned when = v->s.timer;
			if(!(v->flags & A2_SUBINLINE))
				when += frame << 8;
			a2_VoiceSpawn(st, v, when, ins->a1, r[ins->a2] >> 16,
					cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SPAWN:
		  {
			unsigned when = v->s.timer;
			if(!(v->flags & A2_SUBINLINE))
				when += frame << 8;
			a2_VoiceSpawn(st, v, when, ins->a1, ins->a2,
					cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SPAWNDR:
		  {
			unsigned when = v->s.timer;
			if(!(v->flags & A2_SUBINLINE))
				when += frame << 8;
			a2_VoiceSpawn(st, v, when, -1, r[ins->a2] >> 16,
					cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SPAWND:
		  {
			unsigned when = v->s.timer;
			if(!(v->flags & A2_SUBINLINE))
				when += frame << 8;
			a2_VoiceSpawn(st, v, when, -1, ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SENDR:
		  {
			unsigned vid = r[ins->a1] >> 16;
			if(vid >= A2_REGISTERS)
				A2_VMABORT(A2_BADVOICE, "VM:SENDR");
			if(v->sv[vid])
				a2_VoiceSend(st, v->sv[vid],
						(frame << 8) + v->s.timer,
						ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SEND:
			DBG(if(!ins->a2)
				fprintf(stderr, "Weird...! SEND to EP0...\n");)
			if(v->sv[ins->a1])
				a2_VoiceSend(st, v->sv[ins->a1],
						(frame << 8) + v->s.timer,
						ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  case OP_SENDA:
		  {
			A2_voice *sv;
			unsigned when = (frame << 8) + v->s.timer;
			for(sv = v->sub; sv; sv = sv->next)
				a2_VoiceSend(st, sv, when, ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SENDS:
		  {
			int ep = v->program->eps[ins->a2];
			if(ep < 0)
				A2_VMABORT(A2_BADENTRY, "VM:SENDS");
			if((res = a2_VoiceCall(st, v, ep, cargc, cargv, 1)))
				A2_VMABORT(res, "VM:SENDS");
			code = v->program->funcs[v->s.func].code;
			cargc = 0;
			break;
		  }
		  case OP_WAIT:
			if(!v->sv[ins->a1])
				break;	/* No voice to wait for! */
			/* NOTE: This only waits with buffer granularity! */
			if(v->sv[ins->a1]->s.state >= A2_ENDING)
				break;	/* Done! */
			a2_RTApply(&rt, st, v, 255 - v->s.timer, 0);
			v->s.timer = (A2_MAXFRAG - frame) << 8;
			v->s.state = A2_WAITING;
			st->instructions += A2_INSLIMIT - inscount;
			return A2_OK;
		  case OP_KILLR:
		  {
			unsigned vid = r[ins->a1] >> 16;
			if(vid >= A2_REGISTERS)
				A2_VMABORT(A2_BADVOICE, "VM:KILLR");
			if(!v->sv[vid])
				break;
			a2_VoiceKill(st, v->sv[vid]);
			v->sv[vid] = NULL;
			break;
		  }
		  case OP_KILL:
			if(!v->sv[ins->a1])
				break;
			a2_VoiceKill(st, v->sv[ins->a1]);
			v->sv[ins->a1] = NULL;
			break;
		  case OP_KILLA:
		  {
			A2_voice *sv;
			for(sv = v->sub; sv; sv = sv->next)
				a2_VoiceKill(st, sv);
			memset(v->sv, 0, sizeof(v->sv));
			break;
		  }

		/* Message handling */
		  case OP_SLEEP:
			a2_RTApply(&rt, st, v, 255 - v->s.timer, 0);
			v->s.timer = 0xffffffff;
			v->s.state = A2_ENDING;
			st->instructions += A2_INSLIMIT - inscount;
			return A2_OK;
#if 0
TODO:
		  case OP_SLEEP:
			...
			v->s.state = A2_SLEEPING;
			...

		  case OP_WAKE:
		  {
			A2_stackentry *se = v->stack;
			while(se->prev && (se->state == A2_INTERRUPT))
				se = se->prev;
			if(se->state != A2_SLEEPING)
				break;
			++se->pc;	/* Skip out of the SLEEP instruction */
			se->timer = 0;	/* Wake the VM up */
			se->state = A2_RUNNING;
			break;
		  }
#endif
		  case OP_WAKE:
		  {
			A2_stackentry *se = v->stack;
			while(se->prev && (se->state == A2_INTERRUPT))
				se = se->prev;
			if(se->state < A2_ENDING)
				break;
			/* Fall through for the actual wakeup! */
		  }
		  case OP_FORCE:
		  {
			A2_stackentry *se = v->stack;
			while(se->prev && (se->state == A2_INTERRUPT))
				se = se->prev;
			se->pc = ins->a2;
			se->timer = 0;
			se->state = A2_RUNNING;
			break;
		  }

		/* Debugging */
		  case OP_DEBUGR:
			fprintf(stderr, ":: Audiality 2 DEBUG: R%d=%f\t(%p)\n",
					ins->a1, r[ins->a1] * (1.0f / 65536.0f),
					v);
			break;
		  case OP_DEBUG:
			fprintf(stderr, ":: Audiality 2 DEBUG: %f\t(%p)\n",
					ins->a3 * (1.0f / 65536.0f), v);
			break;

		/* Special instructions */
		  case OP_INITV:
			if((res = a2_PopulateVoice(st, v->program, v)))
			{
				st->instructions += A2_INSLIMIT - inscount;
				return res;
			}
			break;
		  case OP_SIZEOF:
			if((res = a2_sizeof_object(st, ins->a2) < 0))
				A2_VMABORT(-res >> 16, "VM:SIZEOF");
			r[ins->a1] = res;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_SIZEOFR:
			if((res = a2_sizeof_object(st, r[ins->a2] >> 16)) < 0)
				A2_VMABORT(-res >> 16, "VM:SIZEOFR");
			r[ins->a1] = res;
			a2_RTMark(&rt, ins->a1);
			break;

		  case A2_OPCODES:
		  default:
			A2_VMABORT(A2_ILLEGALOP, "VM:ILLEGALOP");
		}
		++v->s.pc;
		continue;
	  timing:
		++v->s.pc;
	  timing_interrupt:
		if(v->s.timer + dt >= 256)
		{
			a2_RTApply(&rt, st, v, 255 - v->s.timer, dt);
			v->s.timer += dt;
			v->s.state = A2_WAITING;
			st->instructions += A2_INSLIMIT - inscount;
			return A2_OK;
		}
		v->s.timer += dt;
	}
}
#undef	A2_VMABORT


/* Wrapper for recursive calls to a2_ProcessVoices() */
static inline void a2_ProcessSubvoices(A2_state *st, A2_voice *v,
		unsigned offset, unsigned frames)
{
	if(!v->sub)
		return;
	a2_ProcessVoices(st, &v->sub, offset, frames);
	if(!v->sub)
		if(v->s.state >= A2_ENDING)
			v->s.timer = 0;	/* Notify parent that subs are done! */
}


/* Adding and replacing Process() implementations for the 'inline' unit */
void a2_inline_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_inline *il = a2_inline_cast(u);
	a2_ProcessSubvoices(il->state, il->voice, offset, frames);
}

void a2_inline_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_inline *il = a2_inline_cast(u);
	int i;
	for(i = 0; i < u->noutputs; ++i)
		memset(u->outputs[i] + offset, 0, frames * sizeof(int));
	a2_ProcessSubvoices(il->state, il->voice, offset, frames);
}


/*
 * Process a single voice, alternating between the VM and units (if any) as
 * needed. Note that 'inline' units also run here, so this may recursively do
 * subvoice processing as well!
 */
static inline A2_errors a2_VoiceProcess(A2_state *st, A2_voice *v,
		unsigned offset, unsigned frames)
{
	int s_end = offset;
	int s = offset;
	int s_stop = offset + frames;
	while(s < s_stop)
	{
		A2_unit *u;
		int vmres;
		while(1)
		{
			int res = 0, et;
			if((vmres = a2_VoiceVMProcess(st, v, s)) > A2_END)
				return vmres;
			s_end = s + (v->s.timer >> 8);
			if(!v->events)
				break;
			et = a2_TSDiff(v->events->b.timestamp,
					st->now_fragstart + (s << 8));
			if((et >= 256) || (res = a2_VoiceProcessEvents(st, v)))
			{
				/* Time to do some audio processing! */
				DBG(if(res > A2_END)
					fprintf(stderr, "a2_VoiceProcessEvents(): %s!\n",
							a2_ErrorString(res));)
				/*
				 * Process until VM timer expires, or until the
				 * next event is to be processed; whichever
				 * comes first.
				 */
				if(et < v->s.timer)
					s_end = s + (et >> 8);
				break;
			}
		}
		if(vmres == A2_END)
			return A2_END;
		if(s_end > s_stop)
			s_end = s_stop;
		if(!(frames = s_end - s))
			continue;
		/*
		 * Must adjust the timer BEFORE any subvoice processing if
		 * subvoices are processed in here, or the timer = 0 hack to
		 * notify the parent of terminating voices won't work!
		 */
		v->s.timer -= frames << 8;
		for(u = v->units; u; u = u->next)
			u->Process(u, s, frames);
		s = s_end;
	}
	return A2_OK;
}


void a2_ProcessVoices(A2_state *st, A2_voice **head, unsigned offset,
		unsigned frames)
{
	while(*head)
	{
		A2_voice *v = *head;
		A2_errors res;
		if((res = a2_VoiceProcess(st, v, offset, frames)))
			a2_VoiceFree(st, head);
		else
		{
			/*
			 * When not using the 'inline' unit, subvoices are
			 * processed here instead!
			 */
			if(!(v->flags & A2_SUBINLINE))
				a2_ProcessSubvoices(st, v, offset, frames);
			head = &v->next;
		}
	}
}


/* Pack the fragments from the master bus into the driver output buffers! */
static void a2_ProcessMaster(A2_state *st, unsigned offset, unsigned frames)
{
	int c;
	int32_t **in = st->master->buffers;
	int32_t **bufs = st->audio->buffers;
	for(c = 0; c < st->config->channels; ++c)
		memcpy(bufs[c] + offset, in[c], frames * sizeof(int32_t));
}


A2_errors a2_AudioCallback(A2_audiodriver *driver, unsigned frames)
{
	A2_state *st = (A2_state *)driver->state;
	A2_voice *rootvoice = a2_GetVoice(st, st->rootvoice);
	unsigned offset = 0;
	unsigned remain = frames;
	unsigned t1 = a2_GetTicks();	/* Event timing reference */
	uint64_t t1u = a2_GetMicros();	/* Monitoring: pre DSP timestamp */
	uint64_t dur;

	/* API message processing */
	a2r_PumpEngineMessages(st);

	/* Audio processing */
	while(remain)
	{
		unsigned frag = remain > A2_MAXFRAG ? A2_MAXFRAG : remain;
		a2_ClearBus(st->master, 0, frag);
		a2_ProcessVoices(st, &rootvoice, 0, frag);
		a2_ProcessMaster(st, offset, frag);
		offset += frag;
		remain -= frag;
		st->now_fragstart += frag << 8;
	}
	dur = a2_GetMicros() - t1u;	/* Monitoring: DSP processing time */

	/* Update stats */
	if(dur > st->cputimemax)
		st->cputimemax = dur;
	st->cputimesum += dur;
	++st->cputimecount;
	if(t1u != st->now_micros)
	{
		unsigned ld = dur * 100 / (t1u - st->now_micros);
		if(ld > st->cpuloadmax)
			st->cpuloadmax = ld;
	}
	st->now_micros = t1u;
	if(st->cputimecount)
	{
		st->cputimeavg = st->cputimesum / st->cputimecount;
		if(t1u != st->avgstart)
			st->cpuloadavg = st->cputimeavg * 100 /
					(t1u - st->avgstart);
	}
	if(st->statreset)
	{
		st->statreset = 0;
		st->cputimesum = st->cputimecount = 0;
		st->avgstart = t1u;
	}

	/* Update API message timestamping time reference */
	st->now_frames = st->now_fragstart;
	st->now_ticks = t1;
	st->now_guard = st->now_frames;
	return 0;
}


int a2_Run(A2_state *st, unsigned frames)
{
	if(!st->audio->Run)
		return -A2_NOTIMPLEMENTED;
	return st->audio->Run(st->audio, frames);
}
