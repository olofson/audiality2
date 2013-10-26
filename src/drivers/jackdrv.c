/*
 * jackdrv.c - Audiality 2 JACK audio driver
 *
 * Copyright 2012-2013 David Olofson <david@olofson.net>
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

#ifdef A2_HAVE_JACK

#undef	JACKD_CLIPOUTPUT

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <jack/jack.h>
#include <dlfcn.h>
#include <pthread.h>
#include "jackdrv.h"


/* JACK library entry points */
typedef struct jackd_funcs
{
	void *(*port_get_buffer)(jack_port_t *, jack_nframes_t);
	jack_client_t *(*client_open)(const char *client_name,
			jack_options_t options, jack_status_t *status, ...);
	int (*set_sample_rate_callback)(jack_client_t *client,
			JackSampleRateCallback srate_callback, void *arg);
	int (*set_buffer_size_callback)(jack_client_t *client,
			JackBufferSizeCallback bufsize_callback, void *arg);
	int (*set_process_callback)(jack_client_t *client,
			JackProcessCallback process_callback, void *arg);
	int (*set_xrun_callback)(jack_client_t *client,
			JackXRunCallback xrun_callback, void *arg);
	jack_port_t *(*port_register)(jack_client_t *client,
			const char *port_name, const char *port_type,
			unsigned long flags, unsigned long buffer_size);
	int (*activate)(jack_client_t *client);
	int (*deactivate)(jack_client_t *client);
	int (*client_close)(jack_client_t *client);
	const char **(*get_ports)(jack_client_t *,
			const char *port_name_pattern,
			const char *type_name_pattern, unsigned long flags);
	const char *(*port_name)(const jack_port_t *port);
	int (*connect)(jack_client_t *, const char *source_port,
			const char *destination_port);
} jackd_funcs;


/* Extended A2_audiodriver struct */
typedef struct JACKD_audiodriver
{
	A2_audiodriver	ad;
	jack_client_t	*client;
	jack_port_t	**ports;
	int		overload;
	pthread_mutex_t	mutex;
	void		*libjack;
	jackd_funcs	jack;
} JACKD_audiodriver;


static struct
{
	const char	*name;
	size_t		fn;
} jackfuncs[] = {
	{"jack_port_get_buffer", offsetof(jackd_funcs, port_get_buffer) },
	{"jack_client_open", offsetof(jackd_funcs, client_open) },
	{"jack_set_sample_rate_callback", offsetof(jackd_funcs, set_sample_rate_callback) },
	{"jack_set_buffer_size_callback", offsetof(jackd_funcs, set_buffer_size_callback) },
	{"jack_set_process_callback", offsetof(jackd_funcs, set_process_callback) },
	{"jack_set_xrun_callback", offsetof(jackd_funcs, set_xrun_callback) },
	{"jack_port_register", offsetof(jackd_funcs, port_register) },
	{"jack_activate", offsetof(jackd_funcs, activate) },
	{"jack_deactivate", offsetof(jackd_funcs, deactivate) },
	{"jack_client_close", offsetof(jackd_funcs, client_close) },
	{"jack_get_ports", offsetof(jackd_funcs, get_ports) },
	{"jack_port_name", offsetof(jackd_funcs, port_name) },
	{"jack_connect", offsetof(jackd_funcs, connect) },
	{NULL, 0 }
};

static int jackd_load_jack(JACKD_audiodriver *jd)
{
	int i;
	if(jd->libjack)
	{
		printf("Audiality 2: WARNING: \"libjack.so\" already loaded!\n");
		return 0;
	}
	jd->libjack = dlopen("libjack.so", RTLD_NOW);
	if(!jd->libjack)
	{
		printf("Audiality 2: Could not load \"libjack.so\"!\n");
		return A2_DEVICEOPEN;
	}
	for(i = 0; jackfuncs[i].name; ++i)
	{
		void **fns = (void **)((char *)(&jd->jack) + jackfuncs[i].fn);
		if(!(*fns = dlsym(jd->libjack, jackfuncs[i].name)))
		{
			printf("Audiality 2: Required JACK call '%s' missing!\n",
					jackfuncs[i].name);
			return A2_DEVICEOPEN;
		}
	}
	return 0;
}


/* JACK client callback */
static int jackd_process(jack_nframes_t nframes, void *arg)
{
	int c, i;
	JACKD_audiodriver *jd = (JACKD_audiodriver *)arg;
	A2_audiodriver *driver = &jd->ad;
	A2_config *cfg = driver->driver.config;
	int need_clear = 1;
	if(!jd->overload && driver->Process)
	{
		if(pthread_mutex_trylock(&jd->mutex) == 0)
		{
			driver->Process(driver, nframes);
			pthread_mutex_unlock(&jd->mutex);
			need_clear = 0;
		}
	}
	else if(jd->overload)
		--jd->overload;
	if(need_clear)
		for(c = 0; c < cfg->channels; ++c)
			memset(driver->buffers[c], 0, sizeof(int32_t) * nframes);
	for(c = 0; c < cfg->channels; ++c)
	{
		int32_t *buf = driver->buffers[c];
		jack_default_audio_sample_t *out =
				(jack_default_audio_sample_t *)
				jd->jack.port_get_buffer(jd->ports[c], nframes);
#ifdef JACKD_CLIPOUTPUT
		for(i = 0; i < nframes; ++i)
		{
			int s = buf[i] >> 8;
			if(s < -32768)
				s = -32768;
			else if(s > 32767)
				s = 32767;
			out[i] = s * (1.0f / 32768.0f);
		}
#else
		for(i = 0; i < nframes; ++i)
			out[i] = buf[i] * (1.0f / 8388608.0f);
#endif
	}
	return 0;
}


static void jackd_lock(A2_audiodriver *driver)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)driver;
	if(pthread_mutex_lock(&jd->mutex))
		fprintf(stderr, "WARNING: Audiality 2 API mutex lock failed!\n");
}


static void jackd_unlock(A2_audiodriver *driver)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)driver;
	pthread_mutex_unlock(&jd->mutex);
}


static void jackd_free_buffers(A2_audiodriver *driver)
{
	A2_config *cfg = driver->driver.config;
	if(driver->buffers)
	{
		int c;
		for(c = 0; c < cfg->channels; ++c)
			free(driver->buffers[c]);
		free(driver->buffers);
		driver->buffers = NULL;
	}
}


/* JACK sample rate callback */
static int jackd_srate(jack_nframes_t nframes, void *arg)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)arg;
	A2_audiodriver *driver = &jd->ad;
	A2_config *cfg = driver->driver.config;
	if(cfg->flags & A2_ISOPEN)
		fprintf(stderr, "Audiality 2: WARNING: Sample rate changed from "
				"%d to %d after initialization!\n",
	  			cfg->samplerate, nframes);
	cfg->samplerate = nframes;
	return 0;
}


/* JACK buffer size callback */
static int jackd_bufsize(jack_nframes_t nframes, void *arg)
{
	int c;
	JACKD_audiodriver *jd = (JACKD_audiodriver *)arg;
	A2_audiodriver *driver = &jd->ad;
	A2_config *cfg = driver->driver.config;
	if(cfg->flags & A2_ISOPEN)
		fprintf(stderr, "Audiality 2: WARNING: Buffer size changed from "
				"%d to %d after initialization!\n",
	  			cfg->buffer, nframes);
	pthread_mutex_lock(&jd->mutex);
	jackd_free_buffers(driver);
	cfg->buffer = nframes;
	if(!(driver->buffers = calloc(cfg->channels, sizeof(int32_t *))))
	{
		pthread_mutex_unlock(&jd->mutex);
		return A2_OOMEMORY;
	}
	for(c = 0; c < cfg->channels; ++c)
		if(!(driver->buffers[c] = calloc(cfg->buffer, sizeof(int32_t))))
		{
			pthread_mutex_unlock(&jd->mutex);
			return A2_OOMEMORY;
		}
	pthread_mutex_unlock(&jd->mutex);
	return 0;
}


/* JACK XRun callback */
static int jackd_xrun(void *arg)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)arg;
	/*
	 * Back off a little, to avoid freezing with RT scheduling on single
	 * core systems and/or broken JACK implementations...
	 */
	jd->overload += 1;
	return 0;
}


static void jackd_Close(A2_driver *driver)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)driver;
	if(jd->client)
	{
		jd->jack.deactivate(jd->client);
		jd->jack.client_close(jd->client);
	}
	jackd_free_buffers(&jd->ad);
	free(jd->ports);
	pthread_mutex_destroy(&jd->mutex);
	if(jd->libjack)
	{
		dlclose(jd->libjack);
		jd->libjack = NULL;
	}
	jd->ad.Run = NULL;
	jd->ad.Lock = NULL;
	jd->ad.Unlock = NULL;
}


static A2_errors jackd_autoconnect(A2_audiodriver *driver)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)driver;
	A2_config *cfg = jd->ad.driver.config;
	int c;
	const char **ioports;
	if((ioports = jd->jack.get_ports(jd->client, NULL, NULL,
			JackPortIsPhysical|JackPortIsInput)) == NULL)
	{
		fprintf(stderr, "Audiality 2: Cannot find any physical JACK "
				"playback ports!\n");
		jackd_Close(&jd->ad.driver);
		return A2_DEVICEOPEN;
	}
	for(c = 0; (ioports[c] != NULL) && (c < cfg->channels); ++c)
		if(jd->jack.connect(jd->client, jd->jack.port_name(jd->ports[c]),
				ioports[c]))
			fprintf(stderr, "Audiality 2: Cannot connect output port"
					" %d!\n", c);
	free(ioports);
	return A2_OK;
}

static A2_errors jackd_Open(A2_driver *driver)
{
	JACKD_audiodriver *jd = (JACKD_audiodriver *)driver;
	A2_config *cfg = jd->ad.driver.config;
	int c;
	A2_errors res;
	const char *clientname = "Audiality 2";

	/* Get custom client name, if any was specified */
	for(c = 0; c < driver->optc; ++c)
		if(driver->optv[c][0] != '-')
			clientname = driver->optv[c];

	/* Try to load the JACK API library */
	if((res = jackd_load_jack(jd)))
		return res;

	/* Initialize JACK client */
	jd->ad.Run = NULL;
	pthread_mutex_init(&jd->mutex, NULL);
	jd->ad.Lock = jackd_lock;
	jd->ad.Unlock = jackd_unlock;
	if((jd->client = jd->jack.client_open(clientname, 0, NULL)) == NULL)
	{
		fprintf(stderr, "Audiality 2: JACK server not running?\n");
		return A2_DEVICEOPEN;
	}
	jd->jack.set_sample_rate_callback(jd->client, jackd_srate, driver);
	jd->jack.set_buffer_size_callback(jd->client, jackd_bufsize, driver);
	jd->jack.set_process_callback(jd->client, jackd_process, driver);
	jd->jack.set_xrun_callback(jd->client, jackd_xrun, driver);

	/* Create output ports */
	jd->ports = (jack_port_t **)calloc(cfg->channels, sizeof(jack_port_t *));
	for(c = 0; c < cfg->channels; ++c)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "output-%d", c);
		jd->ports[c] = jd->jack.port_register(jd->client, buf,
				JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	}

	/* Activate client */
	if(jd->jack.activate(jd->client))
	{
		fprintf(stderr, "Audiality 2: Cannot activate JACK client!\n");
		jackd_Close(&jd->ad.driver);
		return A2_DEVICEOPEN;
	}

	/* Auto-connect output ports */
	if(!(cfg->flags & A2_NOAUTOCNX))
		jackd_autoconnect(&jd->ad);

	return A2_OK;
}


A2_driver *a2_jack_audiodriver(A2_drivertypes type, const char *name)
{
	JACKD_audiodriver *jd = calloc(1, sizeof(JACKD_audiodriver));
	A2_driver *d = &jd->ad.driver;
	if(!jd)
		return NULL;
	d->type = A2_AUDIODRIVER;
	d->name = "jack";
	d->Open = jackd_Open;
	d->Close = jackd_Close;
	d->flags = A2_REALTIME;
	return d;
}

#endif /* A2_HAVE_JACK */
