/*
 * audiality2.c - Audiality 2 main file - configuration, open/close etc
 *
 * Copyright 2010-2014, 2016-2017. 2019-2020 David Olofson <david@olofson.net>
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
#include "compiler.h"
#include "wtosc.h"
#include "inline.h"
#include "panmix.h"
#include "xinsert.h"
#include "dbgunit.h"
#include "limiter.h"
#include "fbdelay.h"
#include "filter12.h"
#include "dcblock.h"
#include "waveshaper.h"
#include "fm.h"
#include "dc.h"
#include "env.h"


static void a2_CloseState(A2_state *st);


/*---------------------------------------------------------
	Error handling
---------------------------------------------------------*/

A2_errors a2_last_error = A2_OK;

A2_errors a2_LastError(void)
{
	return a2_last_error;
}

A2_errors a2_LastRTError(A2_interface *i)
{
	A2_state *st = ((A2_interface_i *)i)->state;
	/*
	 * This "should" be synchronized, but if we have multiple errors coming
	 * in, this isn't really going to work reliably anyway.
	 */
	A2_errors res = st->last_rt_error;
	st->last_rt_error = A2_OK;
	return res;
}


/*---------------------------------------------------------
	Versioning
---------------------------------------------------------*/

unsigned a2_LinkedVersion(void)
{
	return A2_VERSION;
}


/*---------------------------------------------------------
	Engine type registry
---------------------------------------------------------*/

A2_errors a2_RegisterType(A2_state *st, A2_otypes otype, const char *name,
		RCHM_destructor_cb destroy, A2_stropen_cb stropen)
{
	A2_errors res;
	RCHM_manager *m = &st->ss->hm;
	A2_typeinfo *ti = (A2_typeinfo *)calloc(1, sizeof(A2_typeinfo));
	if(!ti)
		return A2_OOMEMORY;
	ti->state = st;
	ti->OpenStream = stropen;
	res = (A2_errors)rchm_RegisterType(m, otype, name, destroy, ti);
	if(res)
		free(ti);
	return res;
}


static void type_registry_cleanup(A2_state *st)
{
	int i;
	RCHM_manager *m = &st->ss->hm;
	for(i = 0; i < m->ntypes; ++i)
	{
		A2_typeinfo *ti = (A2_typeinfo *)rchm_TypeUserdata(m, i);
		if(!ti)
			continue;
		free(ti);
	}
}


/*---------------------------------------------------------
	API owned and locked objects
---------------------------------------------------------*/

int a2_UnloadAll(A2_interface *i)
{
	A2_state *st = ((A2_interface_i *)i)->state;
	RCHM_manager *hm;
	RCHM_handle h;
	int count = 0;
	if(!st->ss)
		return 0;
	hm = &st->ss->hm;
	for(h = 0; h < hm->nexthandle; ++h)
	{
		RCHM_handleinfo *hi = rchm_Get(hm, h);
		if(hi && (hi->userbits & A2_APIOWNED))
		{
			DBG(const char *s = a2_String(i, h);)
			hi->userbits &= ~A2_APIOWNED;
			if(rchm_Release(hm, h) == 0)
			{
				++count;
				A2_LOG_DBG(i, "a2_UnloadAll(): Unloaded "
						"object %d %s", h, s);
			}
		}
	}
	return count;
}


/* Removed all A2_LOCKED flags and release any objects with refcount 0 */
static int a2_unlock_all(A2_state *st)
{
	DBG(A2_interface *i = st->interfaces ?
			&(st->interfaces->interface): NULL;)
	RCHM_manager *hm;
	RCHM_handle h;
	int count = 0;
	if(!st->ss)
		return 0;
	hm = &st->ss->hm;
	A2_LOG_DBG(i, "=== a2_unlock_all() ===");
	for(h = 0; h < hm->nexthandle; ++h)
	{
		DBG(const char *s = i ? a2_String(i, h) : "<no interface>";)
		RCHM_handleinfo *hi = rchm_Get(hm, h);
		if(hi && (hi->userbits & A2_LOCKED))
		{
			hi->userbits &= ~A2_LOCKED;
			if(hi->refcount == 0)
				if(rchm_Release(hm, h) == 0)
				{
					++count;
					A2_LOG_DBG(i, "   %d %s", h, s);
				}
		}
	}
	A2_LOG_DBG(i, "=======================");
	return count;
}


/*---------------------------------------------------------
	Open/close
---------------------------------------------------------*/

/* Array of builtin units to register */
static const A2_unitdesc *a2_core_units[] = {
	&a2_inline_unitdesc,
	&a2_wtosc_unitdesc,
	&a2_panmix_unitdesc,
	&a2_xsink_unitdesc,
	&a2_xsource_unitdesc,
	&a2_xinsert_unitdesc,
	&a2_dbgunit_unitdesc,
	&a2_limiter_unitdesc,
	&a2_fbdelay_unitdesc,
	&a2_filter12_unitdesc,
	&a2_dcblock_unitdesc,
	&a2_waveshaper_unitdesc,
	&a2_fm1_unitdesc,
	&a2_fm2_unitdesc,
	&a2_fm3_unitdesc,
	&a2_fm4_unitdesc,
	&a2_fm3p_unitdesc,
	&a2_fm4p_unitdesc,
	&a2_fm2r_unitdesc,
	&a2_fm4r_unitdesc,
	&a2_dc_unitdesc,
	&a2_env_unitdesc,
	NULL
};

static A2_errors a2_OpenSharedState(A2_state *st)
{
	A2_interface *i = &st->interfaces->interface;
	A2_errors res;
	int j;
	A2_compiler *c;
	st->ss = (A2_sharedstate *)calloc(1, sizeof(A2_sharedstate));
	if(!st->ss)
		return A2_OOMEMORY;

	/* Set up state property defaults */
	st->ss->offlinebuffer = 256;

	st->ss->silencelevel = 256;
	st->ss->silencewindow = 256;
	st->ss->silencegrace = 1024;

	st->ss->tabsize = 8;

	/* Init handle manager */
	if((res = (A2_errors)rchm_Init(&st->ss->hm, A2_INITHANDLES)))
		return res;

	/* Register handle types */
	if((res = a2_RegisterBankTypes(st)))
		return res;
	if((res = a2_RegisterUnitTypes(st)))
		return res;
	if((res = a2_RegisterWaveTypes(st)))
		return res;
	if((res = a2_RegisterAPITypes(st)))
		return res;
	if((res = a2_RegisterStreamTypes(st)))
		return res;
	if((res = a2_RegisterXICTypes(st)))
		return res;

	/* Set up the root bank (MUST get handle 0!) */
	res = a2_NewBank(i, "root", A2_LOCKED);
	if(res != A2_ROOTBANK)
		return A2_INTERNAL + 3;	/* Houston, we have a problem...! */

	/* Render builtin waves */
	if((res = a2_InitWaves(i, A2_ROOTBANK)))
		return res;

	/* Register the builtin voice units */
	for(j = 0; a2_core_units[j]; ++j)
	{
		A2_handle h = a2_RegisterUnit(i, a2_core_units[j]);
		if(h < 0)
			return -h;
		if((res = a2_Export(i, A2_ROOTBANK, h, NULL)))
			return res;
	}

	/* Compile builtin programs */
	if(!(c = a2_OpenCompiler(i, 0)))
		return A2_OOMEMORY;
	if((res = a2_CompileString(c, A2_ROOTBANK,
			"export def square pulse50\n"
			"\n"
			"export a2_rootdriver()\n"
			"{\n"
			"	struct {\n"
			"		inline 0 *\n"
			"		panmix * *\n"
			"		xinsert * >\n"
			"	}\n"
			"	2(V) { vol V; ramp vol 100 }\n"
			"	3(PX PY PZ) { pan PX; ramp pan 100 }\n"
			"}\n"
			"\n"
			"export a2_rootdriver_mono()\n"
			"{\n"
			"	struct {\n"
			"		inline 0 2\n"
			"		panmix 2 1\n"
			"		xinsert 1 >\n"
			"	}\n"
			"	2(V) { vol V; ramp vol 100 }\n"
			"	3(PX PY PZ) { pan PX; ramp pan 100 }\n"
			"}\n"
			"\n"
			"export a2_groupdriver()\n"
			"{\n"
			"	struct {\n"
			"		inline 0 *\n"
			"		panmix * *\n"
			"		xinsert * >\n"
			"	}\n"
			"	2(V) { vol V; ramp vol 100 }\n"
			"	3(PX PY PZ) { pan PX; ramp pan 100 }\n"
			"}\n"
			"\n"
			"export a2_terminator() {}\n", "rootbank")))
		return res;
	a2_CloseCompiler(c);

	/* Grab frequently used objects, so we don't have to do that "live" */
	if(!(st->ss->terminator = a2_GetProgram(st,
			a2_Get(i, A2_ROOTBANK, "a2_terminator"))))
		return A2_INTERNAL + 5;
	if(!(st->ss->groupdriver = a2_Get(i, A2_ROOTBANK,
			"a2_groupdriver")))
		return A2_INTERNAL + 6;

	return A2_OK;
}

static void a2_CloseSharedState(A2_state *st)
{
	if(!st->ss)
		return;
	type_registry_cleanup(st);
	rchm_Cleanup(&st->ss->hm);
	free(st->ss->units);
	free(st->ss);
	st->ss = NULL;
}


/* Create an A2_state struct, instantiate drivers as needed, and open them. */
static A2_state *a2_Open0(A2_config *config)
{
	A2_errors res;
	A2_state *st = calloc(1, sizeof(A2_state));
	if(!st)
	{
		a2_last_error = A2_OOMEMORY;
		return NULL;
	}
	st->rootvoice = -1;

	if(!config)
	{
		/* No config! Create a default one. */
		if(!(config = a2_OpenConfig(-1, -1, -1, -1)))
		{
			free(st);
			return NULL;
		}
		config->flags |= A2_AUTOCLOSE;
	}

	st->config = config;

	if(!(st->config->flags & A2_SUBSTATE))
	{
		if(a2_add_api_user() != A2_OK)
		{
			a2_CloseState(st);
			return NULL;
		}
		st->is_api_user = 1;
	}

	/* Get required drivers */
	st->sys = (A2_sysdriver *)a2_GetDriver(config, A2_SYSDRIVER);
	st->audio = (A2_audiodriver *)a2_GetDriver(config, A2_AUDIODRIVER);
	if(!st->sys || !st->audio)
	{
		a2_CloseState(st);
		return NULL;
	}

	/*
	 * If the A2_REALTIME flag is set in the driver, we obey that, because
	 * anything else would just break things, and likely crash the engine!
	 * This way, application code can just forget about the A2_REALTIME
	 * flag, except when using a2_Run() from a different thread.
	 */
	st->config->flags |= st->audio->driver.flags & A2_REALTIME;

	/* Open the system and audio drivers */
	if((res = a2_OpenDriver(&st->sys->driver, A2_AUTOCLOSE)))
	{
		a2_CloseState(st);
		a2_last_error = res;
		return NULL;
	}
	if((res = a2_OpenDriver(&st->audio->driver, A2_AUTOCLOSE)))
	{
		a2_CloseState(st);
		a2_last_error = res;
		return NULL;
	}

	/* Initialize config info fields */
	config->basepitch = a2_F2Pf(A2_MIDDLEC, config->samplerate) *
			65536.0f + 0.5f;

	return st;
}


/* Initialize and start up the actual engine of state 'st'! */
static A2_errors a2_Open2(A2_state *st)
{
	A2_errors res;
	int i;

	/* We set up initial pools by default for realtime states! */
	if(st->config->flags & A2_REALTIME)
	{
		if(!st->config->blockpool)
			st->config->blockpool = A2_INITBLOCKS;
		if(!st->config->voicepool)
			st->config->voicepool = A2_INITVOICES;

		/*
		 * 'eventpool' is a bit special in that the default pool size
		 * is calculated based on buffer size. '-1' tells a2_OpenAPI()
		 * to calculate that as the event system is initialized.
		 */
		if(!st->config->eventpool)
			st->config->eventpool = -1;
	}

	/* Prepare memory block pool */
	for(i = 0; i < st->config->blockpool; ++i)
	{
		A2_block *b = st->sys->RTAlloc(st->sys, sizeof(A2_block));
		if(!b)
			return A2_OOMEMORY;
		b->next = st->blockpool;
		st->blockpool = b;
	}

	/* Set up master audio bus */
	if(!(st->master = a2_AllocBus(st, st->config->channels)))
		return A2_OOMEMORY;

	/* Prepare initial voice pool */
	for(i = 0; i < st->config->voicepool; ++i)
	{
		A2_voice *v = a2_VoiceAlloc(st);
		if(!v)
			return A2_OOMEMORY;
		v->next = st->voicepool;
		st->voicepool = v;
	}

	/* Initialize the realtime control API */
	if((res = a2_OpenAPI(st)))
		return res;
	st->now_ticks = a2_GetTicks();
	st->now_micros = st->avgstart = a2_GetMicros();

	/* Add "master" interface; the one returned by a2_Open(). */
	if(!a2_AddInterface(st, st->config->flags & ~A2_REALTIME))
		return A2_OOMEMORY;

	/*
	 * Link config to "master" interface, now that we have one. (Needed
	 * by some units, when they're registered in 2_OpenSharedState()!)
	 */
	st->config->interface = &st->interfaces->interface;

	/*
	 * For master states, initialize the bank, builtin waves and a2s
	 * compiler.
	 */
	if(!(st->config->flags & A2_SUBSTATE))
	{
		if((res = a2_OpenSharedState(st)))
			return res;
	}
	else
	{
		st->ss = st->parent->ss;
		/* Close any shared unit state for this engine state */
		st->unitstate = malloc(sizeof(A2_unitstate) * st->ss->nunits);
		if(!st->unitstate)
			return A2_OOMEMORY;
		for(i = 0; i < st->ss->nunits; ++i)
			a2_UnitOpenState(st, i);
	}

	/* Initialize RNGs for noise and RAND instructions */
	st->randstate = A2_DEFAULT_RANDSEED;
	st->noisestate = A2_DEFAULT_NOISESEED;

	/* Initialize stats */
	st->tsstatreset = 1;
	st->tsmin = INT32_MAX;
	st->tsmax = INT32_MIN;
	st->statreset = 1;

	/* Start the root voice! */
	st->msdur = st->config->samplerate * 0.001f;
	if((res = a2_init_root_voice(st)))
		return res;

	/* Open remaining drivers, if any. */
	if((res = a2_OpenDrivers(st->config, A2_AUTOCLOSE)))
		return res;

	/* Install the master process callback! */
	st->audio->Lock(st->audio);
	st->audio->state = st;
	st->audio->Process = a2_AudioCallback;
	st->audio->Unlock(st->audio);

	return A2_OK;
}


static A2_errors a2_verify_version(unsigned headerversion)
{
	/* Major and minor versions need to match. */
	if((A2_MAJOR(headerversion) == A2_MAJOR(A2_VERSION)) &&
			(A2_MINOR(headerversion) == A2_MINOR(A2_VERSION)))
	{
		if(A2_MINOR(A2_VERSION) & 1)
		{
			/*
			 * Development branches are assumed to break binary
			 * compatibility with every micro release!
			 */
			if(A2_MICRO(headerversion) == A2_MICRO(A2_VERSION))
				return A2_OK;
		}
		else
		{
			/*
			 * Stable branch; lib needs to be of same or higher
			 * micro version.
			 */
			if(A2_MICRO(headerversion) <= A2_MICRO(A2_VERSION))
				return A2_OK;
		}
	}

	/* Check failed! This will not work. */
	A2_LOG_CRIT("Incompatible library!");
	A2_LOG_CRIT("  This library is version %d.%d.%d.%d",
			A2_MAJOR(A2_VERSION),
			A2_MINOR(A2_VERSION),
			A2_MICRO(A2_VERSION),
			A2_BUILD(A2_VERSION));
	A2_LOG_CRIT("  Application is built for %d.%d.%d.%d",
			A2_MAJOR(headerversion),
			A2_MINOR(headerversion),
			A2_MICRO(headerversion),
			A2_BUILD(headerversion));
	return A2_BADLIBVERSION;
}


A2_interface *a2_OpenVersion(A2_config *config, unsigned headerversion)
{
	A2_errors res;
	A2_state *st;
	A2_interface_i *j;

	if((a2_last_error = a2_verify_version(headerversion)))
		return NULL;

	DUMPSIZES(
		printf("A2_wave:\t%d\n", (int)sizeof(A2_wave));
		printf("A2_bank:\t%d\n", (int)sizeof(A2_bank));
		printf("A2_program:\t%d\n", (int)sizeof(A2_program));
		printf("A2_string:\t%d\n", (int)sizeof(A2_string));
		printf("A2_stackentry:\t%d\n", (int)sizeof(A2_stackentry));
		printf("A2_event:\t%d\n", (int)sizeof(A2_event));
		printf("A2_apimessage:\t%d\n", (int)sizeof(A2_apimessage));
		printf("A2_voice:\t%d\n", (int)sizeof(A2_voice));
		printf("A2_block:\t%d\n", (int)sizeof(A2_block));
		printf("A2_unit:\t%d\n", (int)sizeof(A2_unit));
		printf("A2_unitdesc:\t%d\n", (int)sizeof(A2_unitdesc));
		printf("A2_stream:\t%d\n", (int)sizeof(A2_stream));
		printf("A2_OPCODES:\t%d\n", A2_OPCODES);
		printf("A2_MAXSAVEREGS:\t%d\n", (int)A2_MAXSAVEREGS);
	)
#ifdef DEBUG
	if(config)
	{
		printf("a2_Open(%p) ------\n", config);
		a2_DumpConfig(config);
		printf("------\n");
	}
	else
		printf("a2_Open(NULL)\n");
#endif
	if(!(st = a2_Open0(config)))
		return NULL;
	if((res = a2_Open2(st)))
	{
		A2_LOG_ERR(&st->interfaces->interface,
				"Initialization failed; %s!",
				a2_ErrorString(res));
		a2_CloseState(st);
		a2_last_error = res;
		return NULL;
	}
#ifdef DEBUG
	printf("a2_Open(), resulting config: ------\n");
	a2_DumpConfig(st->config);
	printf("------\n");
#endif
	for(j = st->interfaces; j; j = j->next)
	{
		A2_interface *i = &j->interface;
		a2_PumpMessages(i);
		a2_TimestampReset(i);
	}
	return &st->interfaces->interface;
}


A2_interface *a2_SubState(A2_interface *parent, A2_config *config)
{
	A2_errors res;
	A2_state *st;
	A2_state *pst = ((A2_interface_i *)parent)->state;
	a2_last_error = A2_OK;

	/*
	 * Substate of substate is "ok", but becomes another substate of the
	 * same master state.
	 */
	if(pst->parent)
		pst = pst->parent;

	/*
	 * If no config is provided, create a typical offline streaming setup
	 * based on the master state configuration and properties!
	 */
	if(!config)
	{
		config = a2_OpenConfig(pst->config->samplerate,
				pst->ss->offlinebuffer,
				pst->config->channels, 0);
		if(!config)
			return NULL;	/* Most likely not going to work! --> */
		if((res = a2_AddDriver(config,
				a2_NewDriver(A2_AUDIODRIVER, "buffer"))))
		{
			a2_CloseConfig(config);
			a2_last_error = res;
			return NULL;
		}
		config->flags |= A2_AUTOCLOSE;	/* We close what we open! */
	}
	if(!config)
		return NULL;	/* Most likely not going to work! --> */

	/*
	 * This flag wires some stuff to the master state instead of
	 * initializing local instances.
	 */
	config->flags |= A2_SUBSTATE;

	if(!(st = a2_Open0(config)))
		return NULL;

	/* Link the substate to the master state */
	st->parent = pst;
	st->next = pst->next;
	pst->next = st;

	if((res = a2_Open2(st)))
	{
		A2_LOG_ERR(&st->interfaces->interface,
				"Initialization failed; %s!\n",
				a2_ErrorString(res));
		a2_CloseState(st);
		a2_last_error = res;
		return NULL;
	}
	return &st->interfaces->interface;
}


void a2_Close(A2_interface *i)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	int refs = 0;

	if(--ii->refcount > 0)
		return;

	/* Count remaining non-autoclose interfaces using this state */
	if(st)
	{
		A2_interface_i *iii;
		for(iii = st->interfaces; iii; iii = iii->next)
		{
			if(iii == ii)
				continue;	/* About to be closed. */
			if(iii->flags & (A2_AUTOCLOSE | A2_NOREF))
				continue;	/* These don't count! */
			++refs;
		}
	}

	if(refs || !st)
		a2_RemoveInterface(ii);	/* Close interface only */
	else
		a2_CloseState(st);	/* Close everything! */
}


static void a2_CloseState(A2_state *st)
{
	A2_interface *i = st->interfaces ? &(st->interfaces->interface): NULL;
	int j;

	/* Driver and interface cleanup may send us in here recursively! */
	if(st->is_closing)
		return;

	st->is_closing = 1;

	/* Detach the audio callack */
	if(st->audio)
	{
		A2_audiodriver *d = st->audio;
		if(d->driver.flags & A2_ISOPEN)
		{
			d->Lock(d);
			d->state = NULL;
			d->Process = NULL;
			d->Unlock(d);
		}
	}

	/* Master state? */
	if(!(st->config->flags & A2_SUBSTATE) && st->ss)
	{
		/* Unload the root bank and any other A2_LOCKed objects! */
		a2_unlock_all(st);
		if(i)
			a2_Release(i, A2_ROOTBANK);

		/* Close all substates! */
		while(st->next)
			a2_CloseState(st->next);

		/* Unload any "forgotten" API created objects... */
		if(i)
			a2_UnloadAll(i);
	}

	/* Handle engine/RT error messages, handle release notifications etc */
	if(st->fromapi)
		a2r_PumpEngineMessages(st, st->now_frames);
	/*
	 * We lie about the frame count here, so that events will actually be
	 * processed. Otherwise, we'll leak deleted shared objects that use
	 * a2_WhenAllHaveProcessed() for thread safe cleanup.
	 */
	a2r_ProcessEOCEvents(st, 1);

	/* Close the realtime context of the engine */
	if(st->rootvoice >= 0)
	{
		RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, st->rootvoice);
		if(hi && hi->d.data)
			a2_VoiceFree(st, (A2_voice **)&hi->d.data);
		rchm_Free(&st->ss->hm, st->rootvoice);
	}

	/*
	 * Must do this last thing, because destroying the root voice may
	 * result in xinsert clients and whatnot being disposed of.
	 */
	if(i && st->toapi)
		a2_PumpMessages(i);

	for(j = 0; j < A2_NESTLIMIT; ++j)
		if(st->scratch[j])
			a2_FreeBus(st, st->scratch[j]);
	if(st->master)
		a2_FreeBus(st, st->master);
	while(st->voicepool)
	{
		A2_voice *v = st->voicepool;
		st->voicepool = v->next;
		st->sys->RTFree(st->sys, v);
	}
	while(st->blockpool)
	{
		A2_block *b = st->blockpool;
		st->blockpool = b->next;
		st->sys->RTFree(st->sys, b);
	}

	/* Close any unit shared state for this engine state */
	if(st->unitstate)
	{
		for(j = 0; j < st->ss->nunits; ++j)
			a2_UnitCloseState(st, j);
		free(st->unitstate);
	}

	if(!(st->config->flags & A2_SUBSTATE))
		a2_CloseSharedState(st);

	a2_CloseAPI(st);

	/* Close if we're supposed to, otherwise, detach from state! */
	if(st->config->flags & A2_AUTOCLOSE)
		a2_CloseConfig(st->config);
	else
		st->config->interface = NULL;
	st->config = NULL;

	/* Detach from master state, if any */
	if(st->parent)
	{
		A2_state *s = st->parent->next;
		A2_state *ps = NULL;
		while(s)
		{
			if(s == st)
			{
				if(ps)
					ps->next = st->next;
				else
					st->parent->next = st->next;
				break;
			}
			ps = s;
			s = s->next;
		}
	}

	/* Detach from interfaces, if any, and close any A2_AUTOCLOSE ones */
	while(st->interfaces)
	{
		A2_interface_i *ii = st->interfaces;
		st->interfaces = ii->next;
		ii->state = NULL;
		ii->next = NULL;
		if(ii->flags & A2_AUTOCLOSE)
			a2_RemoveInterface(ii);
	}

	if(st->is_api_user)
		a2_remove_api_user();

	free(st);
}
