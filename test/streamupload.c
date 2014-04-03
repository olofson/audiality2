/*
 * streamupload.c - Audiality 2 wave upload via the stream API
 *
 * Copyright 2013-2014 David Olofson <david@olofson.net>
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
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include "audiality2.h"
#include "waves.h"

/* Fragment size for wave rendering/uploading */
#define	FRAGSIZE	2048


/* Configuration */
const char *audiodriver = "default";
int samplerate = 44100;
int channels = 2;
int audiobuf = 4096;

static int do_exit = 0;


static A2_handle upload_wave(A2_state *st, unsigned len)
{
	A2_errors res;
	A2_handle wh, sh;
	int i;
	int s = 0;
	int16_t buf[FRAGSIZE];
	if((wh = a2_WaveNew(st, A2_WMIPWAVE, 128, 0)) < 0)
		return wh;
	if((sh = a2_OpenStream(st, wh, 0, 0, 0)) < 0)
	{
		a2_Release(st, wh);
		return sh;
	}
	while(s < len)
	{
		for(i = 0; (i < FRAGSIZE) && (s < len); ++i, ++s)
			buf[i] = sin(s * 2.0f * M_PI / 100 +
					sin(s * .0013) * sin(s * .002) * 10) *
					32767.0f;
		if((res = a2_Write(st, sh, A2_I16, buf, i * sizeof(int16_t))))
		{
			a2_Release(st, sh);
			a2_Release(st, wh);
			return -res;
		}
	}
	if((res = a2_Release(st, sh)))
	{
		a2_Release(st, wh);
		return -res;
	}
	return wh;
}


static void usage(const char *exename)
{
	fprintf(stderr,	"\n\nUsage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -b<n>       Audio buffer size (frames)\n"
			"           -r<n>       Audio sample rate (Hz)\n"
			"           -c<n>       Number of audio channels\n\n"
			"           -h          Help\n\n");
}


static void parse_args(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		if(argv[i][0] != '-')
			continue;
		if(strncmp(argv[i], "-d", 2) == 0)
		{
			audiodriver = &argv[i][2];
			printf("[Driver: %s]\n", audiodriver);
		}
		else if(strncmp(argv[i], "-a", 2) == 0)
		{
			audiobuf = atoi(&argv[i][2]);
			printf("[Buffer: %d]\n", audiobuf);
		}
		else if(strncmp(argv[i], "-r", 2) == 0)
		{
			samplerate = atoi(&argv[i][2]);
			printf("[Sample rate: %d]\n", samplerate);
		}
		else if(strncmp(argv[i], "-c", 2) == 0)
		{
			channels = atoi(&argv[i][2]);
			printf("[Channels %d]\n", channels);
		}
		else if(strncmp(argv[i], "-h", 2) == 0)
		{
			usage(argv[0]);
			exit(0);
		}
		else
		{
			fprintf(stderr, "Unknown switch '%s'!\n", argv[i]);
			exit(1);
		}
	}
}


static void breakhandler(int a)
{
	fprintf(stderr, "Stopping...\n");
	do_exit = 1;
}


static void fail(A2_errors err)
{
	fprintf(stderr, "ERROR, Audiality 2 result: %s\n", a2_ErrorString(err));
	exit(100);
}


int main(int argc, const char *argv[])
{
	A2_handle h, ph, vh;
	A2_driver *drv;
	A2_config *cfg;
	A2_state *state;
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	parse_args(argc, argv);

	/* Configure and open master state */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, audiodriver)))
		fail(a2_LastError());
	if(!(cfg = a2_OpenConfig(samplerate, audiobuf, channels,
			A2_TIMESTAMP | A2_REALTIME | A2_STATECLOSE)))
		fail(a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(a2_LastError());
	if(!(state = a2_Open(cfg)))
		fail(a2_LastError());
	if(samplerate != cfg->samplerate)
		printf("Actual master state sample rate: %d (requested %d)\n",
				cfg->samplerate, samplerate);

	/* Load wave player program */
	if((h = a2_Load(state, "data/testprograms.a2s")) < 0)
		fail(-h);
	if((ph = a2_Get(state, h, "PlayTestWave")) < 0)
		fail(-ph);

	/* Generate wave */
	fprintf(stderr, "Generating wave...\n");
	if((h = upload_wave(state, 100000)) < 0)
		fail(-h);

	/* Start playing! */
	fprintf(stderr, "Playing...\n");
	a2_Now(state);
	vh = a2_Start(state, a2_RootVoice(state), ph, 0.0f, 1.0f, h);
	if(vh < 0)
		fail(-vh);

	/* Wait for completion or abort */
	while(!do_exit)
	{
		a2_Now(state);
		a2_Sleep(100);
	}

	a2_Now(state);
	a2_Send(state, vh, 1);
	a2_Sleep(1000);

	/*
	 * Not very nice at all - just butcher everything! But this is supposed
	 * to work without memory leaks or anything, so we may as well test it.
	 */
	a2_Close(state);
	return 0;
}
