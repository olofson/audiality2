/*
 * rtsubstate.c - Audiality 2 realtime substate test
 *
 *	This test runs a master realtime state with a substate that also runs
 *	a realtime driver. That is, two asynchronous realtime states sharing
 *	banks, programs, waves etc.
 *
 *	NOTE:	This test needs a driver/API that supports multiple opens
 *		or multiple soundcards, or the substate will fail to open!
 *
 * Copyright 2013-2016 David Olofson <david@olofson.net>
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
#include "audiality2.h"

/* Configuration */
typedef struct TEST_settings {
	const char	*audiodriver;
	int		samplerate;
	int		channels;
	int		audiobuf;
} TEST_settings;

static TEST_settings settings[2] = {
	{
		"default", 44100, 2, 4096
	},
	{
		"default", 16000, 2, 1024
	}
};

/* State and control */
static A2_state *state = NULL;		/* Engine state */
static A2_state *substate = NULL;	/* Substate of 'state' */

static int do_exit = 0;


static void usage(const char *exename)
{
	fprintf(stderr,	"\n\nUsage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d[s]<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -b[s]<n>    Audio buffer size (frames)\n"
			"           -r[s]<n>    Audio sample rate (Hz)\n"
			"           -c[s]<n>    Number of audio channels\n\n"
			"                       's' is 1 or unpsecified for\n"
			"                       the master state, and 2 for\n"
			"                       the substate.\n\n"
			"           -h          Help\n\n");
}


/* Parse driver selection and configuration switches */
static void parse_args(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		TEST_settings *s;
		int si = 0;
		int skip = 0;
		if(argv[i][0] != '-')
			continue;
		if((strlen(argv[i]) >= 3) &&
				(argv[i][2] >= '1') && (argv[i][2] <= '2'))
		{
			skip = 1;
			si = argv[i][2] - '1';
		}
		s = settings + si;
		if(strncmp(argv[i], "-d", 2) == 0)
		{
			s->audiodriver = &argv[i][2 + skip];
			printf("[Driver %d: %s]\n", si + 1, s->audiodriver);
		}
		else if(strncmp(argv[i], "-b", 2) == 0)
		{
			s->audiobuf = atoi(&argv[i][2 + skip]);
			printf("[Buffer %d: %d]\n", si + 1, s->audiobuf);
		}
		else if(strncmp(argv[i], "-r", 2) == 0)
		{
			s->samplerate = atoi(&argv[i][2 + skip]);
			printf("[Sample rate %d: %d]\n", si + 1, s->samplerate);
		}
		else if(strncmp(argv[i], "-c", 2) == 0)
		{
			s->channels = atoi(&argv[i][2 + skip]);
			printf("[Channels %d: %d]\n", si + 1, s->channels);
		}
		else if(strncmp(argv[i], "-h", 2) == 0)
		{
			usage(argv[0]);
			exit(0);
		}
		else
		{
			fprintf(stderr, "Unknown switch '%s'!\n", argv[i]);
			usage(argv[0]);
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
	fprintf(stderr, "ERROR: %s\n", a2_ErrorString(err));
	if(state)
		a2_Close(state);
	exit(100);
}


int main(int argc, const char *argv[])
{
	A2_handle h, songh;
	A2_driver *drv = NULL;
	A2_config *cfg;
	unsigned flags = A2_TIMESTAMP;
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	parse_args(argc, argv);

	/* Configure and open master state */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, settings[0].audiodriver)))
		fail(a2_LastError());
	if(!(cfg = a2_OpenConfig(settings[0].samplerate, settings[0].audiobuf,
			settings[0].channels, flags | A2_STATECLOSE)))
		fail(a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(a2_LastError());
	if(!(state = a2_Open(cfg)))
		fail(a2_LastError());
	if(settings[0].samplerate != cfg->samplerate)
		printf("Actual master state sample rate: %d (requested %d)\n",
				cfg->samplerate, settings[0].samplerate);

	/* Configure and open substate */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, settings[1].audiodriver)))
		fail(a2_LastError());
	if(!(cfg = a2_OpenConfig(settings[1].samplerate, settings[1].audiobuf,
			settings[1].channels, flags | A2_STATECLOSE)))
		fail(a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(a2_LastError());
	if(!(substate = a2_SubState(state, cfg)))
		fail(a2_LastError());
	if(settings[1].samplerate != cfg->samplerate)
		printf("Actual substate sample rate: %d (requested %d)\n",
				cfg->samplerate, settings[1].samplerate);

	/* Load sounds */
	h = a2_Load(state, "data/k2intro.a2s", 0);
	songh = a2_Get(state, h, "Song");

	/* Start playing! */
	a2_TimestampReset(state);
	a2_TimestampReset(substate);
	a2_Play(state, a2_RootVoice(state), songh);
	a2_Play(substate, a2_RootVoice(substate), songh);

	/* Wait for completion or abort */
	while(!do_exit)
	{
		a2_Sleep(100);
		a2_PumpMessages(state);
	}

	/*
	 * Not very nice at all - just butcher everything! But this is supposed
	 * to work without memory leaks or anything, so we may as well test it.
	 */
	a2_Close(state);
	return 0;
}
