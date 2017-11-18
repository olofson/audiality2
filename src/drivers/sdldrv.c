/*
 * sdldrv.c - Audiality 2 SDL audio driver
 *
 * Copyright 2012-2014, 2017 David Olofson <david@olofson.net>
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


/* Callback for the SDL audio API */
static void sdld_callback(void *ud, Uint8 *stream, int len)
{
	A2_audiodriver *driver = (A2_audiodriver *)ud;
	A2_config *cfg = driver->driver.config;
	int frames = len / 4;
	int c, i;
	Sint16 *out = (Sint16 *)(void *)stream;
	if(driver->Process)
		driver->Process(driver, frames);
	else
		for(c = 0; c < cfg->channels; ++c)
			memset(driver->buffers[c], 0, sizeof(int32_t) * frames);
	for(c = 0; c < cfg->channels; ++c)
	{
		/* Clipping + 16 bit output conversion */
		int32_t *buf = driver->buffers[c];
		for(i = 0; i < frames; ++i)
		{
			int s = buf[i] >> 8;
			if(s < -32768)
				s = -32768;
			else if(s > 32767)
				s = 32767;
			out[i * cfg->channels + c] = s;
		}
	}
}


static void sdld_lock(A2_audiodriver *driver)
{
	SDL_LockAudio();
}


static void sdld_unlock(A2_audiodriver *driver)
{
	SDL_UnlockAudio();
}


static void sdld_Close(A2_driver *driver)
{
	A2_audiodriver *ad = (A2_audiodriver *)driver;
	A2_config *cfg = driver->config;
	SDL_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
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
	A2_audiodriver *ad = (A2_audiodriver *)driver;
	A2_config *cfg = driver->config;
	SDL_AudioSpec as, res_as;
	int c;
	ad->Run = NULL;
	ad->Lock = sdld_lock;
	ad->Unlock = sdld_unlock;
	if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		return A2_DEVICEOPEN;
	as.freq = cfg->samplerate;
	as.format = AUDIO_S16SYS;
	as.channels = cfg->channels;
	as.samples = cfg->buffer;
	as.callback = sdld_callback;
	as.userdata = driver;
	if(SDL_OpenAudio(&as, &res_as) < 0)
	{
		A2_LOG_ERR(cfg->interface, "SDL error: %s", SDL_GetError());
		sdld_Close(driver);
		return A2_DEVICEOPEN;
	}
	if(res_as.format != AUDIO_S16SYS)
	{
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
	if(!(ad->buffers = calloc(cfg->channels, sizeof(int32_t *))))
	{
		sdld_Close(driver);
		return A2_OOMEMORY;
	}
	for(c = 0; c < cfg->channels; ++c)
		if(!(ad->buffers[c] = calloc(cfg->buffer, sizeof(int32_t))))
			{
				sdld_Close(driver);
				return A2_OOMEMORY;
			}
	SDL_PauseAudio(0);
	return A2_OK;
}


A2_driver *a2_sdl_audiodriver(A2_drivertypes type, const char *name)
{
	A2_audiodriver *ad = calloc(1, sizeof(A2_audiodriver));
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
