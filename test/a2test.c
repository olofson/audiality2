/*
 * Audiality 2 test program
 *
 * This code is in the public domain. Do what you like with it. NO WARRANTY!
 *
 * 2011-2016 David Olofson
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include "audiality2.h"
#include "SDL.h"
#include "gui.h"
#ifdef EMSCRIPTEN
#include <emscripten.h>
#include <unistd.h>
#endif

/* Display */
#if (SDL_MAJOR_VERSION >= 2)
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
#else
static SDL_Surface *screen = NULL;
#endif
static int screenw = 800;
static int screenh = 480;
static int screenbpp = 0;
static int screenflags = 0;

/* Oscilloscopes */
static int oscpos = 0;			/* Grab buffer position */
static int plotpos = 0;			/* Plot position */
static Sint32 *osc_left = NULL;		/* Left audio grab buffer */
static Sint32 *osc_right = NULL;	/* Right audio grab buffer */

/* Load info */
static Uint32 now = 0;
static Uint32 lastreset;
static int lasttick;

/* "System" */
static char *audiodriver = NULL;
static int samplerate = 48000;
static int audiobuf = 1024;
static int dbuffer = -1;		/* Sync delay buffer size */
static int a2flags = A2_EXPORTALL;
static int do_exit = 0;

/* Playing and control */
static A2_state *state = NULL;
static A2_handle songbank = -1;
static A2_handle rootvoice = -1;
static A2_handle legatovoice = -1;
static A2_handle chrovoices[12];

/* Selected object: */
static int bankindex = 0;	/* Index of bank in root bank */
static int exportindex = 0;	/* Index in selected bank */
static A2_handle selbank;	/* Handle of selected bank */
static A2_handle selected;	/* Handle of selected export */

static int oct = 0;
static float mod = 0;
static int legato = 0;

static int main_argc;
static const char **main_argv;

static int demo = 0;
static const char *demofiles[] = {
	"data/a2jingle.a2s",
	"data/k2epilogue.a2s",
	"data/k2intro.a2s",
	"data/evilnoises.a2s",
	"data/test.a2s",
	NULL
};


/*-------------------------------------------------------------------
	Audio processing
-------------------------------------------------------------------*/

/* Grab data for the oscilloscopes */
static A2_errors grab_process(int **buffers, unsigned nbuffers, unsigned frames,
		void *userdata)
{
	int i;
	for(i = 0; i < frames; ++i)
		osc_left[(oscpos + i) % dbuffer] = buffers[0][i];
	if(nbuffers >= 2)
		for(i = 0; i < frames; ++i)
			osc_right[(oscpos + i) % dbuffer] = buffers[1][i];
	else
		for(i = 0; i < frames; ++i)
			osc_right[(oscpos + i) % dbuffer] = buffers[0][i];
	oscpos = (oscpos + frames) % dbuffer;
	plotpos = oscpos;
	return A2_OK;
}


/*-------------------------------------------------------------------
	File I/O
-------------------------------------------------------------------*/

static void bankinfo(int row, const char *key)
{
	A2_handle kh = a2_Get(state, selbank, key);
	const char *txt = a2_String(state, kh);
	if(!txt)
		txt = "<undefined>";
	gui_bankinfo(row, key, txt);
}

static void select_object(void)
{
	char buf[128];
	selbank = a2_GetExport(state, songbank, bankindex);
	selected = a2_GetExport(state, selbank, exportindex);
	snprintf(buf, sizeof(buf), "B%d:%s X%d:%s (%s)\n",
		 	bankindex, a2_GetExportName(state, songbank, bankindex),
		 	exportindex, a2_GetExportName(state, selbank, exportindex),
			a2_TypeName(state, a2_TypeOf(state, selected)));
	gui_bankinfo(0, NULL, NULL);
	bankinfo(0, "title");
	bankinfo(1, "version");
	bankinfo(2, "description");
	bankinfo(3, "author");
	bankinfo(4, "copyright");
	bankinfo(5, "license");
	bankinfo(6, "a2sversion");
	gui_message(buf, -1);
}


static void load_sounds(int argc, const char *argv[])
{
	int i;
	if(demo)
	{
		argv = demofiles;
		for(argc = 0; argv[argc]; ++argc)
			;
	}
	else
	{
		/* First one is the application path, if anything... */
		--argc;
		++argv;
	}

	/* Release all songs and create a new song bank */
	a2_Release(state, songbank);
	songbank = a2_NewBank(state, "songbank", 0);
	if(songbank < 0)
	{
		fprintf(stderr, "Couldn't create 'songbank'!\n");
		exit(1);
	}

	for(i = 0; i < argc; ++i)
	{
		char buf[128];
		A2_handle h;
		if(argv[i][0] == '-')
			continue;
		h = a2_Load(state, argv[i], 0);
		if(h < 0)
		{
			snprintf(buf, sizeof(buf), "Could not load \"%s\"!"
					" (%s)\n", argv[i],
					a2_ErrorString(-h));
			fputs(buf, stderr);
			gui_message(buf, -1);
			continue;
		}
		snprintf(buf, sizeof(buf), "%d: \"%s\" - %s - %s\n",
				h, a2_Name(state, h),
				a2_String(state, a2_Get(state, h, "author")),
				a2_String(state, a2_Get(state, h, "title")));
		fputs(buf, stdout);
		gui_message(buf, -1);
		/*
		 * We export them from our songbank, so we can find them via
		 * the introspection API instead of managing raw handles! This
		 * also assigns ownership to the bank, so we don't have to
		 * explicitly release each song we've loaded.
		 */
		a2_Export(state, songbank, h, a2_Name(state, h));
	}
	select_object();
}


static void note_on(int note)
{
	if(legato)
	{
		if(legatovoice < 0)
			legatovoice = a2_Start(state, rootvoice,
					selected, note / 12.0f + oct, 1, mod);
		else
		{
			a2_Send(state, legatovoice, 2, note / 12.0f + oct);
			a2_Send(state, legatovoice, 1, 1);
		}
	}
	else
		chrovoices[note] = a2_Start(state, rootvoice,
				selected, note / 12.0f + oct, 1, mod);
}


static void note_off(int note)
{
	if(!legato)
	{
		a2_Send(state, chrovoices[note], 1);
		a2_Release(state, chrovoices[note]);
		/*
		 * Just to make sure we don't have don't have stray invalid
		 * handles in case keyboard events run out of sync.
		 */
		chrovoices[note] = -1;
	}
}


#if (SDL_MAJOR_VERSION >= 2)
# define	NO_KEY_REPEAT	if(event.key.repeat) break;
#else
# define	NO_KEY_REPEAT
#endif

static void handle_events(int argc, const char *argv[])
{
	char buf[128];
	SDL_Event event;
	while(SDL_PollEvent(&event))
	{
		switch(event.type)
		{
		  case SDL_QUIT:
			do_exit = 1;
			break;
		  case SDL_MOUSEBUTTONDOWN:
			break;
		  case SDL_MOUSEBUTTONUP:
			break;
		  case SDL_KEYDOWN:
			switch(event.key.keysym.sym)
			{
			  case SDLK_ESCAPE:
				do_exit = 1;
				break;
			  case SDLK_LCTRL:
			  case SDLK_RCTRL:
			  case SDLK_LSHIFT:
			  case SDLK_RSHIFT:
				legato = 1;
				break;
			  case SDLK_F1:
			  case SDLK_F2:
			  case SDLK_F3:
			  case SDLK_F4:
			  case SDLK_F5:
			  case SDLK_F6:
			  case SDLK_F7:
			  case SDLK_F8:
			  case SDLK_F9:
			  case SDLK_F10:
			  case SDLK_F11:
			  case SDLK_F12:
				NO_KEY_REPEAT
				note_on(event.key.keysym.sym - SDLK_F1);
				break;
			  case SDLK_p:
				NO_KEY_REPEAT
				if(legatovoice >= 0)
					a2_Release(state, legatovoice);
				legatovoice = a2_Start(state, rootvoice,
						selected, oct, 1, mod);
				break;

			  /* Killing voices */
			  case SDLK_y:
				NO_KEY_REPEAT
				a2_Kill(state, legatovoice);
				legatovoice = -1;
				break;
			  case SDLK_k:
				NO_KEY_REPEAT
				a2_KillSub(state, rootvoice);
				legatovoice = -1;
				memset(chrovoices, -1, sizeof(chrovoices));
				break;

			  /* Reload all scripts */
			  case SDLK_r:
				load_sounds(argc, argv);
				break;

			  /* Bank selection */
			  case SDLK_END:
			  case SDLK_DELETE:
				if(bankindex > 0)
					--bankindex;
				select_object();
				break;
			  case SDLK_HOME:
			  case SDLK_INSERT:
				++bankindex;
				while((a2_GetExport(state, songbank,
						bankindex) < 0) && bankindex)
					--bankindex;
				select_object();
				break;

			  /* Export selection */
			  case SDLK_PLUS:
			  case SDLK_KP_PLUS:
				++exportindex;
				while((a2_GetExport(state, selbank,
						exportindex) < 0) && exportindex)
					--exportindex;
				select_object();
				break;
			  case SDLK_MINUS:
			  case SDLK_KP_MINUS:
				if(exportindex > 0)
					--exportindex;
				select_object();
				break;

			  /* Octave selection */
			  case SDLK_PAGEUP:
				++oct;
				snprintf(buf, sizeof(buf), "Octave: %d\n", oct);
				gui_message(buf, -1);
				break;
			  case SDLK_PAGEDOWN:
				--oct;
				snprintf(buf, sizeof(buf), "Octave: %d\n", oct);
				gui_message(buf, -1);
				break;

			  /* Modulation control */
			  case SDLK_KP_MULTIPLY:
				mod += .1;
				a2_SendSub(state, rootvoice, 3, mod);
				snprintf(buf, sizeof(buf), "Modulation: %f\n", mod);
				gui_message(buf, -1);
				break;
			  case SDLK_KP_DIVIDE:
				mod -= .1;
				a2_SendSub(state, rootvoice, 3, mod);
				snprintf(buf, sizeof(buf), "Modulation: %f\n", mod);
				gui_message(buf, -1);
				break;
			  default:
				break;
			}
			break;
		  case SDL_KEYUP:
			switch(event.key.keysym.sym)
			{
			  case SDLK_LCTRL:
			  case SDLK_RCTRL:
			  case SDLK_LSHIFT:
			  case SDLK_RSHIFT:
				legato = 0;
				a2_Send(state, legatovoice, 1, 0);
				a2_Release(state, legatovoice);
				legatovoice = -1;
				break;
			  case SDLK_F1:
			  case SDLK_F2:
			  case SDLK_F3:
			  case SDLK_F4:
			  case SDLK_F5:
			  case SDLK_F6:
			  case SDLK_F7:
			  case SDLK_F8:
			  case SDLK_F9:
			  case SDLK_F10:
			  case SDLK_F11:
			  case SDLK_F12:
				note_off(event.key.keysym.sym - SDLK_F1);
				break;
			  default:
				break;
			}
			break;
		  default:
			break;
		}
	}
}


static void update_main(int dt)
{
	int v;
	int w = (screenw - 270) / 2 - 8;
	gui_oscilloscope(osc_left, dbuffer, plotpos, 270, 8, w, 128);
	gui_oscilloscope(osc_right, dbuffer, plotpos, 270 + w + 8, 8, w, 128);
	if(now - lastreset > 300)
	{
		a2_GetStateProperty(state, A2_PCPULOADAVG, &v);
		gui_cpuload(v);
		a2_GetStateProperty(state, A2_PACTIVEVOICESMAX, &v);
		gui_voices(v);
		a2_GetStateProperty(state, A2_PINSTRUCTIONS, &v);
		gui_instructions(v * 1000 / (now - lastreset));
		a2_SetStateProperty(state, A2_PCPULOADAVG, 0);
		a2_SetStateProperty(state, A2_PACTIVEVOICESMAX, 0);
		a2_SetStateProperty(state, A2_PINSTRUCTIONS, 0);
		lastreset = now;
	}
}


static void parse_args(int argc, const char *argv[])
{
	int i;
	demo = 1;
	for(i = 1; i < argc; ++i)
	{
		if(argv[i][0] != '-')
		{
			demo = 0;	/* Filename specified - no demo! */
			continue;
		}
		if(strncmp(argv[i], "-f", 2) == 0)
		{
#if (SDL_MAJOR_VERSION >= 2)
			screenflags |= SDL_WINDOW_FULLSCREEN;
#else
			screenflags |= SDL_FULLSCREEN;
#endif
			printf("[Fullscreen mode]\n");
		}
		else if(strncmp(argv[i], "-w", 2) == 0)
		{
			screenw = atoi(&argv[i][2]);
			printf("[Display width: %d]\n", screenw);
		}
		else if(strncmp(argv[i], "-h", 2) == 0)
		{
			screenh = atoi(&argv[i][2]);
			printf("[Display height: %d]\n", screenh);
		}
		else if(strncmp(argv[i], "-bpp", 4) == 0)
		{
			screenbpp = atoi(&argv[i][4]);
			printf("[Display bpp: %d]\n", screenbpp);
		}
		else if(strncmp(argv[i], "-d", 2) == 0)
		{
			free(audiodriver);
			audiodriver = strdup(&argv[i][2]);
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
		else
		{
			fprintf(stderr, "Unknown switch '%s'!\n", argv[i]);
			exit(1);
		}
	}
}


static void breakhandler(int a)
{
	do_exit = 1;
}


static void fail(A2_errors err)
{
	fprintf(stderr, "ERROR, Audiality 2 result: %s\n", a2_ErrorString(err));
	exit(100);
}

static void do_frame(void)
{
	int tick = SDL_GetTicks();
	int dt = tick - lasttick;
	now += dt;
	lasttick = tick;
	a2_PumpMessages(state);
	handle_events(main_argc, main_argv);
	update_main(dt);
	gui_refresh();
}

int main(int argc, char *argv[])
{
	A2_config *cfg;
	A2_handle tcb;
	main_argc = argc;
	main_argv = (const char **)argv;

	parse_args(argc, (const char **)argv);
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		exit(1);
	atexit(SDL_Quit);
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Open window */
#if (SDL_MAJOR_VERSION >= 2)
	if(SDL_CreateWindowAndRenderer(screenw, screenh, screenflags,
			&window, &renderer))
		exit(2);
	SDL_SetWindowTitle(window, "Audiality 2 Test Program");
#else
	screen = SDL_SetVideoMode(screenw, screenh, screenbpp, screenflags);
	SDL_WM_SetCaption("Audiality 2 Test Program", "Audiality 2");
#endif
	SDL_ShowCursor(1);
#if (SDL_MAJOR_VERSION >= 2)
	if(gui_open(renderer) < 0)
#else
	if(gui_open(screen) < 0)
#endif
		exit(3);

	/*
	 * Set up an Audiality 2 configuration with the desired drivers.
	 *
	 * Note the A2_STATECLOSE flag, that causes the engine state to take
	 * responsibility for closing the config.
	 */
	if(!(cfg = a2_OpenConfig(samplerate, audiobuf, 2,
			a2flags | A2_STATECLOSE)))
		fail(a2_LastError());
	if(audiodriver)
		if(a2_AddDriver(cfg, a2_NewDriver(A2_AUDIODRIVER, audiodriver)))
			fail(a2_LastError());

	/* Open an Audiality 2 engine state! */
	if(!(state = a2_Open(cfg)))
		fail(a2_LastError());
	rootvoice = a2_RootVoice(state);

	/* Set up the oscilloscopes */
	if(dbuffer < 0)
		dbuffer = audiobuf * 3;
	osc_left = calloc(dbuffer, sizeof(Sint32));
	osc_right = calloc(dbuffer, sizeof(Sint32));
	if(!osc_left || !osc_right)
	{
		fprintf(stderr, "Couldn't allocate visualization buffers!\n");
		exit(4);
	}
	if((tcb = a2_SinkCallback(state, rootvoice, grab_process, NULL)) < 0)
		fail(-tcb);

	gui_draw_screen();

	/* Load sounds - example songs, or specified file */
	load_sounds(argc, (const char **)argv);

	/* GUI main loop */
	lastreset = lasttick = SDL_GetTicks();

#ifdef EMSCRIPTEN
	emscripten_set_main_loop(do_frame, -1, 1);
#else
	while(!do_exit)
	{
		do_frame();
		SDL_Delay(10);
	}
#endif

	a2_Release(state, tcb);
	a2_Release(state, songbank);
	a2_Close(state);
	gui_close();
	SDL_Quit();
	free(audiodriver);
	return 0;
}
