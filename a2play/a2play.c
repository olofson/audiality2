/*
 * a2play.c - Audiality 2 command line player
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
#include "a2_units.h"

/* Silence detection window size (seconds) */
#define	SILENCEWINDOW	0.25f

/* Read chunk size for reading form stdin */
#define	READ_CHUNK_SIZE	256

static int readstdin = 0;

/* Configuration */
static const char *audiodriver = "default";
static int samplerate = 44100;
static int channels = 2;
static int audiobuf = 4096;
static int a2flags = A2_EXPORTALL | A2_TIMESTAMP;

/* State and control */
static A2_state *state = NULL;	/* Engine state */
static A2_handle module = -1;	/* Handle of last loaded module */

static double stoptime = 0.0f;	/* (Need final sample rate for stopframes!) */
static int stopframes = 0;
static int playedframes = 0;
static int silencelevel = -1;
static unsigned silencewindow;

static int do_exit = 0;

/* Silence detector state */
static unsigned lastpeak = 0;	/* Frames since last peak > abs(silencelevel) */


/*-------------------------------------------------------------------
	Audio processing
-------------------------------------------------------------------*/

/* Callback for automatic stop logic */
static A2_errors sink_process(int **buffers, unsigned nbuffers,
		unsigned frames, void *userdata)
{
	int i, j;

	/* Count sample frames processed */
	playedframes += frames;

	/* Silence tracking */
	lastpeak += frames;
	for(j = 0; j < nbuffers; ++j)
		for(i = 0; i < frames; ++i)
		{
			int s = buffers[j][i];
			if((s > silencelevel) || (-s > silencelevel))
				lastpeak = 0;
		}

	/* Stop conditions */
	if(stopframes && (silencelevel >= 0))
	{
		if((playedframes >= stopframes) && (lastpeak >= silencewindow))
			do_exit = 1;
	}
	else if(stopframes && (playedframes >= stopframes))
		do_exit = 1;
	else if((silencelevel >= 0) && (lastpeak >= silencewindow))
		do_exit = 1;

	return A2_OK;
}


/*-------------------------------------------------------------------
	Object info printouts
-------------------------------------------------------------------*/

static void print_info(int indent, const char *xname, A2_handle h)
{
	int i;
	A2_handle x;
	A2_otypes t = a2_TypeOf(state, h);
	const char *name = a2_Name(state, h);
	int has_exports = a2_GetExport(state, h, 0) >= 1;
	if(has_exports)
	{
		for(i = 0; i < indent; ++i)
			printf("    ");
		printf("---------------------------------------------\n");
	}
	for(i = 0; i < indent; ++i)
		printf("    ");
	if(xname)
		printf("%-16s", xname);
	else if(name)
		printf("%-16s", name);
	else
		printf("%-16d", h);
	printf("%-12s", a2_TypeName(state, t));
	switch(t)
	{
	  case A2_TBANK:
		break;
	  case A2_TWAVE:
	  {
		A2_wave *w = a2_GetWave(state, h);
		switch(w->type)
		{
		  case A2_WOFF:
			printf("OFF     ");
			break;
		  case A2_WNOISE:
			printf("NOISE   ");
			break;
		  case A2_WWAVE:
			printf("WAVE    ");
			break;
		  case A2_WMIPWAVE:
			printf("MIPWAVE ");
			break;
		}
		switch(w->type)
		{
		  case A2_WOFF:
			break;
		  case A2_WNOISE:
			printf(" per: %-8d", w->period);
			break;
		  case A2_WWAVE:
		  case A2_WMIPWAVE:
			printf(" per: %-8d size: %-8d", w->period,
					w->d.wave.size[0]);
			if(w->flags & A2_LOOPED)
				printf(" LOOPED");
			break;
		}
		break;
	  }
	  case A2_TPROGRAM:
		/* TODO: New properties for argument count, defaults etc! */
		break;
	  case A2_TUNIT:
	  {
		const A2_crdesc *crd;
		const A2_unitdesc *ud = a2_GetUnitDescriptor(state, h);

		/* Inputs and outputs */
		if(ud->maxinputs)
		{
			if(ud->mininputs == ud->maxinputs)
				printf("i: %d     ", ud->mininputs);
			else
				printf("i: %d..%d  ", ud->mininputs,
						ud->maxinputs);
		}
		else
			printf("i: ----  ");
		if(ud->maxoutputs)
		{
			if(ud->minoutputs == ud->maxoutputs)
				printf("o: %d     ", ud->minoutputs);
			else
				printf("o: %d..%d  ", ud->minoutputs,
						ud->maxoutputs);
		}
		else
			printf("o: ----  ");

		/* Control registers */
		for(crd = ud->registers; crd->name; crd++)
			printf(" %s", crd->name);
		break;
	  }
	  case A2_TSTRING:
		printf("%s", a2_String(state, h));
		break;
	  case A2_TSTREAM:
	  case A2_TDETACHED:
	  case A2_TNEWVOICE:
	  case A2_TVOICE:
	  case A2_TXICLIENT:
		break;
	}
	printf("\n");
	if(has_exports)
	{
		for(i = 0; i < indent; ++i)
			printf("    ");
		printf("---------------------------------------------\n");
		for(i = 0; (x = a2_GetExport(state, h, i)) >= 0; ++i)
			print_info(indent + 1, a2_GetExportName(state, h, i), x);
		for(i = 0; i < indent; ++i)
			printf("    ");
	}
}


/*-------------------------------------------------------------------
	Loading
-------------------------------------------------------------------*/

/* Try to load each non-option argument as an .a2s file. */
static void load_sounds(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		A2_handle h;
		if(argv[i][0] == '-')
			continue;
		if((h = a2_Load(state, argv[i], 0)) < 0)
		{
			fprintf(stderr, "Could not load \"%s\"! (%s)\n",
					argv[i], a2_ErrorString(-h));
			continue;
		}
		a2_Export(state, A2_ROOTBANK, h, NULL);
		module = h;
		fprintf(stderr, "Loaded \"%s\" - %s - %s\n",
				a2_Name(state, h),
				a2_String(state, a2_Get(state, h, "author")),
				a2_String(state, a2_Get(state, h, "title")));
	}
}


/*-------------------------------------------------------------------
	Playing
-------------------------------------------------------------------*/

/* Parse the body of a -p switch; <name>[,pitch[,vel[,mod[,...]]]] */
static int play_sound(const char *cmd)
{
	A2_handle h, vh;
	int i;
	char program[256];
	float a[A2_MAXARGS];
	int ia[A2_MAXARGS];
	int cnt;
	printf("Playing %s/%s...\n", a2_Name(state, module), cmd);
	cnt = sscanf(cmd, "%[A-Za-z0-9_],%f,%f,%f,%f,%f,%f,%f,%f", program,
			a, a + 1, a + 2, a + 3, a + 4, a + 5, a + 6, a + 7);
	if(cnt <= 0)
	{
		fprintf(stderr, "a2play: -p switch with no arguments!\n");
		return -1;
	}
	if((h = a2_Get(state, module, program)) < 0)
	{
		fprintf(stderr, "a2play: a2_Get(\"%s\"): %s\n", program,
				a2_ErrorString(-h));
		return -2;
	}
	for(i = 0; i < cnt - 1; ++i)
		ia[i] = a[i] * 65536.0f;
	if((vh = a2_Starta(state, a2_RootVoice(state), h, cnt - 1, ia)) < 0)
	{
		fprintf(stderr, "a2play: a2_Starta(): %s\n",
				a2_ErrorString(-vh));
		return -3;
	}
	return 1;
}


/*
 * Handle any -p switches. If no -p switches are found, try the default entry
 * point "Song".
 *
 * Returns 1 if one or more programs were started, a negative value if there
 * was an error, otherwise 0.
 */
static int play_sounds(int argc, const char *argv[])
{
	int i;
	int res = 0;
	for(i = 1; i < argc; ++i)
	{
		if(argv[i][0] != '-')
			continue;
		if(strncmp(argv[i], "-p", 2) == 0)
		{
			if(play_sound(&argv[i][2]) < 0)
				return -1;
			res = 1;
		}
		else if(strncmp(argv[i], "-xr", 3) == 0)
			print_info(0, NULL, A2_ROOTBANK);
		else if(strncmp(argv[i], "-x", 2) == 0)
			print_info(0, NULL, module);
	}
	if(!res && (a2_Get(state, module, "Song") >= 0))
		res = play_sound("Song");
	return res;
}


/*-------------------------------------------------------------------
	User interface
-------------------------------------------------------------------*/

static void print_version(const char *exename)
{
	unsigned v = a2_LinkedVersion();
	fprintf(stderr, "Audiality 2 a2play\n"
			"Linked against v%d.%d.%d.%d\n",
			A2_MAJOR(v),
			A2_MINOR(v),
			A2_MICRO(v),
			A2_BUILD(v));
	v = a2_HeaderVersion();
	fprintf(stderr, "Compiled against v%d.%d.%d.%d\n",
			A2_MAJOR(v),
			A2_MINOR(v),
			A2_MICRO(v),
			A2_BUILD(v));
	fprintf(stderr, "Copyright 2016 David Olofson\n");
}


static void usage(const char *exename)
{
	fprintf(stderr,	"\n");
	print_version(exename);
	fprintf(stderr,	"\nUsage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -d?         List available drivers\n"
			"           -b<n>       Audio buffer size (frames)\n"
			"           -r<n>       Audio sample rate (Hz)\n"
			"           -c<n>       Number of audio channels\n"
			"           -s          Read input from stdin\n"
			"           -p<name>[,arg[,arg[,...]]]\n"
			"                       Run program <name> with the "
			"specified arguments\n"
			"           -st<n>      Stop time (seconds)\n"
			"           -sl<n>      Stop level (1.0 <==> clip)\n"
			"           -x          Print module exports\n"
			"           -xr         Print engine root exports\n"
			"           -v          Print engine and header "
			"versions\n"
			"           -h          Help\n\n");
}


static void list_drivers(void)
{
	A2_regdriver *rd = NULL;
	printf("Available drivers:\n");
	while((rd = a2_FindDriver(A2_ANYDRIVER, rd)))
		printf("    %s (%s)\n", a2_DriverName(rd),
				a2_DriverTypeName(a2_DriverType(rd)));
}


/* Parse driver selection and configuration switches */
static void parse_args(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		if(argv[i][0] != '-')
			continue;
		if(strncmp(argv[i], "-d", 2) == 0)
		{
			if(argv[i][2] == '?')
			{
				list_drivers();
				exit(0);
			}
			audiodriver = &argv[i][2];
			printf("[Audio driver: %s]\n", audiodriver);
		}
		else if(strncmp(argv[i], "-b", 2) == 0)
		{
			audiobuf = atoi(&argv[i][2]);
			printf("[Audio buffer: %d]\n", audiobuf);
		}
		else if(strncmp(argv[i], "-r", 2) == 0)
		{
			samplerate = atoi(&argv[i][2]);
			printf("[Audio sample rate: %d]\n", samplerate);
		}
		else if(strncmp(argv[i], "-c", 2) == 0)
		{
			channels = atoi(&argv[i][2]);
			printf("[Audio channels: %d]\n", channels);
		}
		else if(strncmp(argv[i], "-p", 2) == 0)
			continue;
		else if(strncmp(argv[i], "-s", 2) == 0)
		{
			readstdin = 1;
			printf("[Reading stdin]\n");
		}
		else if(strncmp(argv[i], "-st", 3) == 0)
		{
			stoptime = atof(&argv[i][3]);
			printf("[Stop after: %f s]\n", stoptime);
		}
		else if(strncmp(argv[i], "-sl", 3) == 0)
		{
			double sl = atof(&argv[i][3]);
			silencelevel = sl * 8388608.0f;
			/* In case of LSB rounding errors in some effect... */
			if(silencelevel < 1)
				silencelevel = 1;
			printf("[Stop below: %f]\n", sl);
		}
		else if(strncmp(argv[i], "-xr", 3) == 0)
			continue;
		else if(strncmp(argv[i], "-x", 2) == 0)
			continue;
		else if(strncmp(argv[i], "-h", 2) == 0)
		{
			usage(argv[0]);
			exit(0);
		}
		else if(strncmp(argv[i], "-v", 2) == 0)
		{
			print_version(argv[0]);
			exit(0);
		}
		else
		{
			fprintf(stderr, "a2play: Unknown switch '%s'!\n",
					argv[i]);
			usage(argv[0]);
			exit(1);
		}
	}
}


static void breakhandler(int a)
{
	fprintf(stderr, "a2play: (Break!)\n");
	do_exit = 1;
}


static void fail(A2_errors err)
{
	fprintf(stderr, "a2play: ERROR: %s\n", a2_ErrorString(err));
	exit(100);
}


/*
 * Read from 'f' until EOF, returning the buffer pointer and size through
 * 'buf' and 'len'. Returns 1 on success, 0 on failure.
 */
static int read_until_eof(FILE *f, char **buf, size_t *len)
{
	*len = 0;
	*buf = NULL;
	while(1)
	{
		size_t count;
		char *nb = realloc(*buf, *len + READ_CHUNK_SIZE + 1);
		if(!nb)
			break;
		*buf = nb;
		count = fread(*buf + *len, 1, READ_CHUNK_SIZE, f);
		*len += count;
		if(count < READ_CHUNK_SIZE)
		{
			if(feof(f))
			{
				(*buf)[*len] = 0;
				return 1;
			}
			break;
		}
	}
	/* Failure! */
	fprintf(stderr, "a2play: Could not read input!\n");
	free(*buf);
	*len = 0;
	*buf = NULL;
	return 0;
}


/*-------------------------------------------------------------------
	main()
-------------------------------------------------------------------*/

int main(int argc, const char *argv[])
{
	A2_driver *drv = NULL;
	A2_config *cfg;
	A2_handle tcb;

	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	if(argc <= 1)
	{
		fprintf(stderr, "a2play: No arguments specified!\n");
		usage(argv[0]);
		exit(1);
	}
	parse_args(argc, argv);

	/* Configure and open engine */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, audiodriver)))
		fail(a2_LastError());
	if(!(cfg = a2_OpenConfig(samplerate, audiobuf, channels,
			a2flags | A2_STATECLOSE)))
		fail(a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(a2_LastError());
	if(!(state = a2_Open(cfg)))
		fail(a2_LastError());
	if(samplerate != cfg->samplerate)
		printf("a2play: Actual sample rate: %d (requested %d)\n",
				cfg->samplerate, samplerate);
	stopframes = stoptime * cfg->samplerate;
	silencewindow = SILENCEWINDOW * cfg->samplerate;

	/* Load sounds */
	load_sounds(argc, argv);
	if(readstdin)
	{
		A2_handle h;
		char *buf;
		size_t len;
		if(!read_until_eof(stdin, &buf, &len))
		{
			a2_Close(state);
			return 1;
		}
		if((h = a2_LoadString(state, buf, "stdin")) < 0)
		{
			fprintf(stderr, "Could not compile A2S from stdin!"
					" (%s)\n", a2_ErrorString(-h));
			a2_Close(state);
			return 1;
		}
		a2_Export(state, A2_ROOTBANK, h, NULL);
		module = h;
		free(buf);
	}

	/* Start playing! */
	a2_TimestampReset(state);
	tcb = a2_SinkCallback(state, a2_RootVoice(state), sink_process, NULL);
	if(tcb < 0)
		fail(-tcb);
	if(play_sounds(argc, argv) != 1)
	{
		a2_Close(state);
		return 1;
	}

	if(a2flags & A2_REALTIME)
	{
		printf("a2play: Realtime mode.\n");

		/* Wait for completion or abort */
		while(!do_exit)
		{
			a2_PumpMessages(state);
			a2_Sleep(10);
		}

		fprintf(stderr, "a2play: Stopping...\n");

		/* Fade out */
		a2_TimestampReset(state);
		a2_Send(state, a2_RootVoice(state), 2, 0.0f);
		/* FIXME: Account for actual output latency! */
		a2_Sleep(200);
	}
	else
	{
		printf("a2play: Offline mode.\n");
		while(!do_exit)
		{
			a2_Run(state, cfg->buffer);
			a2_PumpMessages(state);
		}
	}
	fprintf(stderr, "a2play: Stopped. %d sample frames played.\n", 
			playedframes);

	/* Close and clean up */
	a2_Close(state);
	return 0;
}
