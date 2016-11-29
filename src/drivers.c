/*
 * drivers.c - Audiality 2 device driver and configuration interfaces
 *
 * Copyright 2012-2014, 2016 David Olofson <david@olofson.net>
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

#ifdef DEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "internals.h"

/* Builtin system drivers */
#include "mallocdrv.h"

/* Builtin audio drivers */
#include "sdldrv.h"
#include "jackdrv.h"
#include "dummydrv.h"
#include "bufferdrv.h"

#define A2_DEFAULT_SYSDRIVER	a2_malloc_sysdriver

#ifdef A2_HAVE_SDL
# define A2_DEFAULT_AUDIODRIVER	a2_sdl_audiodriver
#elif defined(A2_HAVE_JACK)
# define A2_DEFAULT_AUDIODRIVER	a2_jack_audiodriver
#else
# define A2_DEFAULT_AUDIODRIVER	a2_dummy_audiodriver
#endif

static void a2_reset_driver_registry(void);


/*---------------------------------------------------------
	Configuration
---------------------------------------------------------*/

void a2_DumpConfig(A2_config *c)
{
#ifdef DEBUG
	A2_driver *d = c->drivers;
	printf("    samplerate: %d\n", c->samplerate);
	printf("        buffer: %d\n", c->buffer);
	printf("      channels: %d\n", c->channels);
	printf("         flags:");
	if(c->flags & A2_TIMESTAMP) printf(" TIMESTAMP");
	if(c->flags & A2_NOAUTOCNX) printf(" NOAUTOCNX");
	if(c->flags & A2_REALTIME) printf(" REALTIME");
	if(c->flags & A2_SUBSTATE) printf(" SUBSTATE");
	if(c->flags & A2_ISOPEN) printf(" ISOPEN");
	if(c->flags & A2_STATECLOSE) printf(" STATECLOSE");
	if(c->flags & A2_CFGCLOSE) printf(" CFGCLOSE");
	printf("\n");
	printf("      poolsize: %d\n", c->poolsize);
	printf("     blockpool: %d\n", c->blockpool);
	printf("     voicepool: %d\n", c->voicepool);
	printf("   eventpool: %d\n", c->eventpool);
	printf("       drivers:\n");
	while(d)
	{
		const char *rt;
		if(d->flags & A2_REALTIME)
			rt = ", REALTIME";
		else
			rt = "";
		printf("           \"%s\" (%s%s)\n", d->name,
				a2_DriverTypeName(d->type), rt);
		d = d->next;
	}
#endif
}


A2_config *a2_OpenConfig(int samplerate, int buffer, int channels, int flags)
{
	A2_config *c = (A2_config *)calloc(1, sizeof(A2_config));
	a2_last_error = A2_OK;
	if(!c)
	{
		a2_last_error = A2_OOMEMORY;
		return NULL;
	}

	c->flags = flags;
	if(samplerate >= 0)
		c->samplerate = samplerate;
	else
		c->samplerate = 48000;
	if(buffer >= 0)
		c->buffer = buffer;
	else
		c->buffer = 1024;
	if(channels >= 0)
		c->channels = channels;
	else
		c->channels = 2;

#ifdef DEBUG
	printf("Created A2_config %p: ------\n", c);
	a2_DumpConfig(c);
	printf("------\n");
#endif
	return c;
}


A2_errors a2_AddDriver(A2_config *config, A2_driver *driver)
{
	a2_last_error = A2_OK;
	/* Just so we can safely nest a non-safe a2_GetBuiltinDriver() call */
	if(!driver)
		return (a2_last_error =  A2_NODRIVER);
	driver->next = config->drivers;
	config->drivers = driver;
	driver->config = config;
	return A2_OK;
}


A2_driver *a2_GetDriver(A2_config *config, A2_drivertypes type)
{
	A2_errors res;
	A2_driver *d;
	a2_last_error = A2_OK;
	for(d = config->drivers; d; d = d->next)
		if(d->type == type)
			return d;
	if(!(d = a2_NewDriver(type, NULL)))
		return NULL;
	if((res = a2_AddDriver(config, d)))
	{
		a2_last_error = res;
		a2_DestroyDriver(d);
		return NULL;
	}
	return d;
}


A2_errors a2_OpenDrivers(A2_config *config, int flags)
{
	A2_errors res;
	A2_driver *d;
	a2_last_error = A2_OK;
	for(d = config->drivers; d; d = d->next)
		if((res = a2_OpenDriver(d, flags)))
			return res;
	return A2_OK;
}


A2_errors a2_CloseDrivers(A2_config *config, int mask)
{
	A2_driver *d;
	for(d = config->drivers; d; d = d->next)
	{
		if(mask & !(mask & d->flags))
			continue;	/* Not a match! */
		a2_CloseDriver(d);
	}
	return A2_OK;
}


static void a2_destroy_drivers(A2_config *config)
{
	while(config->drivers)
	{
		A2_driver *d = config->drivers;
		config->drivers = d->next;
		a2_DestroyDriver(d);
	}
}


void a2_CloseConfig(A2_config *config)
{
	a2_CloseDrivers(config, 0);
	a2_destroy_drivers(config);
	if(config->interface)
		((A2_interface_i *)config->interface)->state->config = NULL;
	free(config);
}


/*---------------------------------------------------------
	Drivers
---------------------------------------------------------*/

A2_errors a2_OpenDriver(A2_driver *driver, int flags)
{
	A2_errors res;
	if(driver->flags & A2_ISOPEN)
		return A2_ALREADYOPEN;
	driver->flags |= driver->config->flags & A2_INITFLAGS;
	if((res = driver->Open(driver)))
		return res;
	driver->flags |= A2_ISOPEN | flags;
	return A2_OK;
}

void a2_CloseDriver(A2_driver *driver)
{
	if(driver->flags & A2_ISOPEN)
	{
		driver->Close(driver);
		driver->flags &= ~A2_ISOPEN;
	}
}


/*---------------------------------------------------------
	Driver registry
---------------------------------------------------------*/

struct A2_regdriver
{
	A2_regdriver	*next;
	A2_drivertypes	type;
	int		builtin;	/* 1: This is a built-in driver! */
	const char	*name;
	A2_newdriver_cb	create;
};

/* Array of static registry entries for the built-in drivers */
static A2_regdriver a2_builtin_drivers[] = {
	{ NULL, A2_SYSDRIVER, 1, "default", A2_DEFAULT_SYSDRIVER },
	{ NULL, A2_SYSDRIVER, 1, "malloc", a2_malloc_sysdriver },
/*FIXME*/{ NULL, A2_SYSDRIVER, 1, "realtime", a2_malloc_sysdriver },
	{ NULL, A2_AUDIODRIVER, 1, "default", A2_DEFAULT_AUDIODRIVER },
#ifdef A2_HAVE_SDL
	{ NULL, A2_AUDIODRIVER, 1, "sdl", a2_sdl_audiodriver },
#endif
#ifdef A2_HAVE_JACK
	{ NULL, A2_AUDIODRIVER, 1, "jack", a2_jack_audiodriver },
#endif
	{ NULL, A2_AUDIODRIVER, 1, "dummy", a2_dummy_audiodriver },
	{ NULL, A2_AUDIODRIVER, 1, "buffer", a2_buffer_audiodriver },
	{ NULL, 0, 1, NULL, NULL }
};

static A2_mutex a2_driver_registry_mtx;

/*
 * 1: builtins already registered
 *
 * NOTE:
 *	It's not enough to check a2_driver_registry for NULL, as it's
 *	perfectly possible to have an empty registry! It could be that no
 *	built-in drivers have been compiled in, or that the application has
 *	removed them all with a2_UnregisterDriver(NULL).
 */
static int a2_builtins_registered = 0;

static A2_regdriver *a2_driver_registry = NULL;


A2_errors a2_drivers_open(void)
{
	return a2_MutexOpen(&a2_driver_registry_mtx);
}


void a2_drivers_close(void)
{
	a2_reset_driver_registry();
	a2_MutexClose(&a2_driver_registry_mtx);
}


static void a2_register_builtin_drivers(void)
{
	int i;
	if(a2_builtins_registered)
		return;

	/*
	 * We just grab them from the array, reinitializing and pushing them
	 * onto the registry stack, so we avoid allocating memory blocks that
	 * Valgrind and the like will whine about later. The registry is not
	 * explicitly initialized by the application, so the application
	 * shouldn't be expected to clean it up either!
	 */
	for(i = 0; a2_builtin_drivers[i].name; ++i)
	{
		A2_regdriver *rd = a2_builtin_drivers + i;
		rd->next = a2_driver_registry;
		a2_driver_registry = rd;
	}
	a2_builtins_registered = 1;
}

A2_errors a2_RegisterDriver(A2_drivertypes type, const char *name,
		A2_newdriver_cb create)
{
	A2_regdriver *rd;
	A2_errors res = a2_add_api_user();
	if(res != A2_OK)
		return res;

	a2_MutexLock(&a2_driver_registry_mtx);

	/*
	 * Make sure the built-ins are in place first! Otherwise, search order
	 * will be undefined, and more seriously, registering a user driver
	 * first thing would result in the built-in drivers never being
	 * registered.
	 */
	a2_register_builtin_drivers();

	rd = (A2_regdriver *)calloc(1, sizeof(A2_regdriver));
	if(!rd)
	{
		a2_MutexUnlock(&a2_driver_registry_mtx);
		a2_remove_api_user();
		return A2_OOMEMORY;
	}

	rd->type = type;
	rd->name = strdup(name);
	if(!rd->name)
	{
		a2_MutexUnlock(&a2_driver_registry_mtx);
		a2_remove_api_user();
		free(rd);
		return A2_OOMEMORY;
	}
	rd->create = create;
	rd->next = a2_driver_registry;
	a2_driver_registry = rd;
	a2_MutexUnlock(&a2_driver_registry_mtx);
	/*
	 * NOTE: If you register a driver, you're considered an API user until
	 *       that driver is unregistered. Thus, no a2_remove_api_user().
	 */
	return A2_OK;
}


static void a2_unregister_all_drivers(void)
{
	while(a2_driver_registry)
	{
		A2_regdriver *rd = a2_driver_registry;
		a2_driver_registry = rd->next;
		if(!rd->builtin)
		{
			free((void *)rd->name);
			free(rd);
			a2_remove_api_user();
		}
	}
}


static A2_errors a2_unregister_driver(const char *name)
{
	A2_regdriver *rd, *rdd;
	if(!name)
	{
		/*
		 * NOTE:
		 *	Unregistering ALL drivers is supposed to get us in a
		 *	state where there really are no drivers at all, and the
		 *	host application can start adding its own. Thus, if
		 *	this call is made before the built-in drivers have been
		 *	registered, we need to fake that by saying that they
		 *	have been - or they'll be pulled right in as the first
		 *	user driver is registered!
		 */
		a2_unregister_all_drivers();
		a2_builtins_registered = 1;
		return A2_OK;
	}

	/*
	 * This might seem a bit weird, but without this, applications would
	 * have to perform a dummy operation just to get a list of drivers to
	 * remove anything from!
	 */
	a2_register_builtin_drivers();

	/* First? */
	rd = a2_driver_registry;
	if(strcmp(name, rd->name) == 0)
	{
		a2_driver_registry = rd->next;
		if(!rd->builtin)
		{
			free((void *)rd->name);
			free(rd);
			a2_remove_api_user();
		}
		return A2_OK;
	}

	/* Any other item? */
	while(rd->next && (strcmp(name, rd->next->name) != 0))
		rd = rd->next;
	if(!rd->next)
		return A2_NOTFOUND;
	rdd = rd->next;
	rd->next = rd->next->next;
	if(!rd->builtin)
	{
		free((void *)rdd->name);
		free(rdd);
		a2_remove_api_user();
	}
	return A2_OK;
}


A2_errors a2_UnregisterDriver(const char *name)
{
	A2_errors res = a2_add_api_user();
	if(res != A2_OK)
		return res;
	a2_MutexLock(&a2_driver_registry_mtx);
	res = a2_unregister_driver(name);
	a2_MutexUnlock(&a2_driver_registry_mtx);
	a2_remove_api_user();
	return res;
}


static void a2_reset_driver_registry(void)
{
	a2_unregister_driver(NULL);
	a2_builtins_registered = 0;
}

void a2_ResetDriverRegistry(void)
{
	if(a2_add_api_user() != A2_OK)
		return;
	a2_MutexLock(&a2_driver_registry_mtx);
	a2_reset_driver_registry();
	a2_MutexUnlock(&a2_driver_registry_mtx);
	a2_remove_api_user();
}


static void a2_remove_options(A2_driver *driver)
{
	int i;
	for(i = 0; i < driver->optc; ++i)
		free((void *)driver->optv[i]);
	free(driver->optv);
}


static void a2_parse_options(A2_driver *driver, const char *opts)
{
	int i;
	const char *o = opts;
	a2_remove_options(driver);
	if(!strlen(opts))
		return;

	/* Count options and allocate array */
	while(1)
	{
		++driver->optc;
		if((o = strchr(o, ',')))
			++o;
		else
			break;
	}
	driver->optv = (const char **)malloc(
			driver->optc * sizeof (const char *));
	if(!driver->optv)
	{
		/* FIXME: Error handling...? */
		driver->optc = 0;
		return;
	}

	/* Extract options */
	for(i = 0; opts; ++i)
	{
		int len;
		if((o = strchr(opts, ',')))
			len = o - opts;
		else
			len = strlen(opts);
		if(!(driver->optv[i] = strndup(opts, len)))
		{
			/* FIXME: Error handling...? */
			a2_remove_options(driver);
			return;
		}
		if(!o)
			break;
		opts = o + 1;
	}
}


A2_driver *a2_NewDriver(A2_drivertypes type, const char *nameopts)
{
	int namelen;
	const char *optstart;
	A2_regdriver *rd;
	A2_driver *drv = NULL;
	if(a2_add_api_user() != A2_OK)
		return NULL;
	a2_MutexLock(&a2_driver_registry_mtx);
	a2_last_error = A2_OK;
	a2_register_builtin_drivers();
	if(!nameopts)
		nameopts = "default";
	optstart = strchr(nameopts, ',');
	if(optstart)
		namelen = optstart - nameopts;
	else
		namelen = strlen(nameopts);
	for(rd = a2_driver_registry; rd; rd = rd->next)
		if((rd->type == type) && !strncmp(rd->name, nameopts, namelen))
		{
			drv = rd->create(type, nameopts);
			if(optstart)
				a2_parse_options(drv, optstart + 1);
			break;
		}
	if(!drv)
		a2_last_error = A2_DRIVERNOTFOUND;
	a2_MutexUnlock(&a2_driver_registry_mtx);
	return drv;
}


void a2_DestroyDriver(A2_driver *driver)
{
	a2_CloseDriver(driver);
	a2_remove_options(driver);
	if(driver->Destroy)
		driver->Destroy(driver);
	else
		free(driver);
	a2_remove_api_user();
}


A2_regdriver *a2_FindDriver(A2_drivertypes type, A2_regdriver *prev)
{
	/*
	 * NOTE:
	 *	This is kind of nasty when used without any other API users,
	 *	because the driver registry will be initialized and closed on
	 *	every call. However, this shouldn't be a problem, as the only
	 *	driver entries available in that situation are the statically
	 *	allocated ones, and they'll be linked in the exact same way in
	 *	every initialization.
	 */
	if(a2_add_api_user() != A2_OK)
		return NULL;
	a2_MutexLock(&a2_driver_registry_mtx);
	a2_register_builtin_drivers();
	if(prev)
		prev = prev->next;
	else
		prev = a2_driver_registry;
	if(type != A2_ANYDRIVER)
		while(prev && prev->type != type)
			prev = prev->next;
	a2_MutexUnlock(&a2_driver_registry_mtx);
	a2_remove_api_user();
	return prev;
}


const char *a2_DriverName(A2_regdriver *driver)
{
	return driver->name;
}


A2_drivertypes a2_DriverType(A2_regdriver *driver)
{
	return driver->type;
}


const char *a2_DriverTypeName(A2_drivertypes dt)
{
	switch(dt)
	{
	  case A2_ANYDRIVER:	return "<any>";
	  case A2_SYSDRIVER:	return "SYS";
	  case A2_AUDIODRIVER:	return "AUDIO";
	  default:		return "<unknown>";
	}
}
