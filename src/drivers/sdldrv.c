/*
 * sdldrv.c - Audiality 2 SDL audio driver
 *
 * Copyright 2012-2014, 2017, 2022 David Olofson <david@olofson.net>
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

#ifdef A2_HAVE_SDL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "SDL.h"
#include "sdldrv.h"
#include "a2_log.h"


/* Extended A2_audiodriver struct */
typedef struct SDLD_audiodriver
{
	A2_audiodriver		ad;
	SDL_AudioDeviceID	device;
} SDLD_audiodriver;


/* Callback for the SDL audio API */
static void sdld_callback(void *ud, Uint8 *stream, int len)
{
	SDLD_audiodriver *sd = (SDLD_audiodriver *)ud;
	A2_audiodriver *ad = &sd->ad;
	A2_config *cfg = ad->driver.config;
	int frames = len / 8;
	int c, i;
	float *out = (float *)(void *)stream;
	if(ad->Process)
		ad->Process(ad, frames);
	else
		for(c = 0; c < cfg->channels; ++c)
			memset(ad->buffers[c], 0, sizeof(float) * frames);
	for(c = 0; c < cfg->channels; ++c)
	{
		/*
		 * NOTE: We're expecting SDL or the underlying API to do any
		 * necessary clipping here!
		 */
		float *buf = ad->buffers[c];
		for(i = 0; i < frames; ++i)
			out[i * cfg->channels + c] = buf[i];
	}
}


static void sdld_lock(A2_audiodriver *driver)
{
	SDLD_audiodriver *sd = (SDLD_audiodriver *)driver;
	SDL_LockAudioDevice(sd->device);
}


static void sdld_unlock(A2_audiodriver *driver)
{
	SDLD_audiodriver *sd = (SDLD_audiodriver *)driver;
	SDL_UnlockAudioDevice(sd->device);
}


static void sdld_Close(A2_driver *driver)
{
	SDLD_audiodriver *sd = (SDLD_audiodriver *)driver;
	A2_audiodriver *ad = &sd->ad;
	A2_config *cfg = driver->config;
	SDL_CloseAudioDevice(sd->device);
#if 0
	/* We should probably not close this here, as there may be other
	 * devices still up, with the SDL 2.0 API.
	 */
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
	if(ad->buffers)
	{
		int c;
		for(c = 0; c < cfg->channels; ++c)
			free(ad->buffers[c]);
		free(ad->buffers);
	}
	ad->Run = NULL;
	ad->Lock = NULL;
	ad->Unlock = NULL;
}


static A2_errors sdld_Open(A2_driver *driver)
{
	SDLD_audiodriver *sd = (SDLD_audiodriver *)driver;
	A2_audiodriver *ad = &sd->ad;
	A2_config *cfg = driver->config;
	SDL_AudioSpec as, res_as;
	int c;
	ad->Run = NULL;
	ad->Lock = sdld_lock;
	ad->Unlock = sdld_unlock;
	if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		return A2_DEVICEOPEN;
	memset(&as, 0, sizeof(as));
	as.freq = cfg->samplerate;
	as.format = AUDIO_F32SYS;
	as.channels = cfg->channels;
	as.samples = cfg->buffer;
	as.callback = sdld_callback;
	as.userdata = ad;
	sd->device = SDL_OpenAudioDevice(NULL, 0, &as, &res_as,
			SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
			SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if(!sd->device)
	{
		A2_LOG_ERR(cfg->interface, "SDL error: %s", SDL_GetError());
		sdld_Close(driver);
		return A2_DEVICEOPEN;
	}
	if(res_as.format != as.format)
	{
		/* Should no longer be possible, but... */
		sdld_Close(driver);
		return A2_BADFORMAT;
	}
	cfg->samplerate = res_as.freq;
	cfg->channels = res_as.channels;
	cfg->buffer = res_as.samples;
	if(cfg->buffer < 1)
	{
		/* What did you do, SDL...!? This will not work. */
		sdld_Close(driver);
		return A2_BADBUFSIZE;
	}
	if(!(ad->buffers = calloc(cfg->channels, sizeof(float *))))
	{
		sdld_Close(driver);
		return A2_OOMEMORY;
	}
	for(c = 0; c < cfg->channels; ++c)
		if(!(ad->buffers[c] = calloc(cfg->buffer, sizeof(float))))
			{
				sdld_Close(driver);
				return A2_OOMEMORY;
			}
	SDL_PauseAudioDevice(sd->device, 0);
	return A2_OK;
}


A2_driver *a2_sdl_audiodriver(A2_drivertypes type, const char *name)
{
	A2_audiodriver *ad = calloc(1, sizeof(SDLD_audiodriver));
	A2_driver *d = &ad->driver;
	if(!ad)
		return NULL;
	d->type = A2_AUDIODRIVER;
	d->name = "sdl";
	d->Open = sdld_Open;
	d->Close = sdld_Close;
	d->flags = A2_REALTIME;
	return d;
}

#endif /* A2_HAVE_SDL */
