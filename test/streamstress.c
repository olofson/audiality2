/*
 * streamstress.c - Stress test of asynchronous streaming via xinsert
 *
 * Copyright 2014 David Olofson <david@olofson.net>
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
#define	FRAGSIZE	256

/* Number of simultaneous independent streams */
#define	STREAMS		8

/* Stream poll/write period (ms) */
#define	POLLPERIOD	100

/* Stream buffer size (ms) */
#define	STREAMBUFFER	1000

/* Configuration */
const char *audiodriver = "default";
int samplerate = 44100;
int channels = 2;
int audiobuf = 4096;

static int do_exit = 0;

/* Streaming voice program handle */
A2_handle streamprogram;

/* Stream oscillator */
typedef struct STREAMOSC
{
	A2_handle	stream;		/* Send stream handle */
	float		ph1, dph1;	/* Operator 1 */
	float		ph2, dph2;	/* Operator 2 */
	float		depth;		/* Modulation depth */
} STREAMOSC;


/* Create and start streaming oscillator. Returns 0 (A2_OK) on success. */
static A2_errors so_Start(A2_state *st, STREAMOSC *so)
{
	A2_handle vh = a2_Start(st, a2_RootVoice(st), streamprogram,
			1.0f,				/* velocity */
			a2_Rand(st, 2.0f) - 1.0f,	/* pan */
			200.0f + a2_Rand(st, 2000.0f));	/* duration */
	if(vh < 0)
		return vh;
	so->stream = a2_OpenReturn(st, vh, 0,
			samplerate * STREAMBUFFER / 1000, 0);
	if(so->stream < 0)
		return so->stream;
	so->ph1 = so->ph2 = 0.0f;
	so->dph1 = 2.0f * M_PI * (.5f + a2_Rand(st, 1.0f)) / samplerate;
	so->dph2 = 2.0f * M_PI * (.5f + a2_Rand(st, 1.0f)) / samplerate;
	so->depth = a2_Rand(st, 10.0f);
	a2_Release(st, vh);
	return A2_OK;
}


/*
 * Run streaming oscillator, generating FRAGSIZE sample frames at a time.
 * Returns 0 (A2_OK) on success. If the voice has terminated and closed the
 * stream on the engine side, a new voice is started, and a new stream set up.
 */
static A2_errors so_Run(A2_state *st, STREAMOSC *so)
{
	A2_errors res;
	int n;
	if((so->stream <= 0) || ((n = a2_Available(st, so->stream)) < 0))
	{
		if(so->stream >= 0)
			a2_Release(st, so->stream);
		if((res = so_Start(st, so)))
			return res;
		n = a2_Available(st, so->stream);
	}
	while(n >= FRAGSIZE)
	{
		int i;
		int32_t buf[FRAGSIZE];
		for(i = 0; i < FRAGSIZE; ++i)
		{
			buf[i] = sin(so->ph1 + sin(so->ph2) * so->depth) *
					32767.0f * 256.0f;
			so->ph1 += so->dph1;
			so->ph2 += so->dph2;
		}
		if((res = a2_Write(st, so->stream, A2_I24, buf, sizeof(buf))))
			return -res;
	}
	return A2_OK;
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
	A2_handle h;
	A2_driver *drv;
	A2_config *cfg;
	A2_state *state;
	STREAMOSC streams[STREAMS];
	memset(streams, 0, sizeof(streams));
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

	/* Load streaming voice program */
	if((h = a2_Load(state, "data/testprograms.a2s")) < 0)
		fail(-h);
	if((streamprogram = a2_Get(state, h, "StreamVoice")) < 0)
		fail(-streamprogram);

	/* Start playing! */
	fprintf(stderr, "Playing...\n");

	/* Wait for completion or abort */
	while(!do_exit)
	{
		int i;
		a2_Now(state);
		for(i = 0; i < STREAMS; ++i)
			so_Run(state, &streams[i]);
		a2_Sleep(POLLPERIOD);
	}

	/* Fade root voice down to 0. (It will do a 100 ms ramp!) */
	a2_Now(state);
	a2_Send(state, a2_RootVoice(state), 2, 0.0f);
	a2_Sleep(200);

	a2_Close(state);
	return 0;
}
