/*
 * internals.h - Audiality 2 internals
 *
 * Copyright 2010-2013 David Olofson <david@olofson.net>
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
 *
WARNING: The Audiality 2 parser uses setjmp()/longjmp() for error handling.
WARNING: Calls with the a2c_ prefix MUST ONLY be used with a2c_Try()!
 */

#ifndef A2_INTERNALS_H
#define A2_INTERNALS_H

#include <string.h>
#include <stddef.h>
#include "units.h"
#include "drivers.h"
#include "waves.h"
#include "rchm.h"
#include "sfifo.h"

#ifdef _WIN32
# include <windows.h>
# include <mmsystem.h>
#else
# include <sched.h>
# include <sys/time.h>
# include <sys/wait.h>
#endif

#ifdef DEBUG
#	include	<stdio.h>
#	include	<assert.h>
#	define	DBG(x)			/* General debug output */
#	define	NUMMSGS(x)		/* Message order tracking */
#	define	MSGTRACK(x)		/* Track origin of messages */
#	define	DUMPMSGS(x)		/* Dump messages from the VM/msg loop */
#	define	DUMPCODE(x)		/* Enable compiler VM code output */
#	define	DUMPLSTRINGS(x)		/* Lexer string processing output */
#	define	DUMPSTRUCT(x)		/* Compiler voice structure dumping */
#	define	DUMPSTRUCTRT(x)		/* Realtime voice structure dumping */
#	define	DUMPCODERT(x)		/* Enable realtime VM code dumping */
#	undef	CERRDIE			/* Die in assert(0) on compile errors */
#else
#	define	DBG(x)
#	define	NUMMSGS(x)
#	define	MSGTRACK(x)
#	define	DUMPMSGS(x)
#	define	DUMPCODE(x)
#	define	DUMPLSTRINGS(x)
#	define	DUMPSTRUCT(x)
#	define	DUMPSTRUCTRT(x)
#	define	DUMPCODERT(x)
#	undef	CERRDIE
#endif

/*
 * API message FIFO size coefficients. Units are *messages* - not bytes!
 *
 * NOTE:
 *	The calculated FIFO size is a hard limit! The FIFO cannot be scaled up
 *	after initialization in the current implementation.
 *
 * A2_MINMESSAGES is the minimum size regardless of audio buffering/latency;
 * effectively the maximum number of messages that can reliably be sent
 * back-to-back.
 *
 * A2_TIMEMESSAGES in the number of additional messages per second to
 * allocate buffer space for. This is needed when using large audio buffers, as
 * the message FIFO is only checked once per audio buffer!
 */
#define	A2_MINMESSAGES		256
#define	A2_TIMEMESSAGES		1000

/*
 * Initial event pool size coefficients, corresponding to the above.
 *
 * NOTE:
 *	This is just the initial pool size. The pool will be extended
 *	automatically as needed, although that currently means memory
 *	allocation from the audio context, which is a very bad idea in
 *	a low latency realtime application!
 *	   Also note that A2_TIMEEVENTS is mostly motivated by the
 *	API, as internal voice->voice event lifetime is governed by
 *	A2_MAXFRAG.
 */
#define	A2_MINEVENTS		256
#define	A2_TIMEEVENTS		1000

/* Enable high fidelity wavetable oscillator */
#define	A2_HIFI

/* Default tick duration; corresponds to 'tempo 120 4' */
#define	A2_DEFAULTTICK		(125 << 16)

/* Waveform period for full bandwidth down to 20 Hz fundamental */
#define	SC_WPER			2048

/*
 * Max number of VM instructions to run without processing samples. If exceeded,
 * the voice will be killed, to avoid freezing the whole sound engine.
 */
#define A2_INSLIMIT		10000

/*
 * Maximum allowed child voice nesting depth. (Recursive explosion inhibitor.)
 */
#define	A2_NESTLIMIT		32

/* Default initial pool sizes for A2_REALTIME states */
#define	A2_INITHANDLES		256
#define	A2_INITVOICES		256
#define	A2_INITBLOCKS		512

/* Size of temporary string buffers (bytes) */
#define	A2_TMPSTRINGSIZE	256

typedef struct A2_typeinfo A2_typeinfo;
typedef struct A2_string A2_string;
typedef struct A2_bank A2_bank;
typedef struct A2_program A2_program;
typedef struct A2_stackentry A2_stackentry;
typedef struct A2_bus A2_bus;
typedef struct A2_event A2_event;
typedef struct A2_voice A2_voice;
typedef struct A2_compiler A2_compiler;
typedef struct A2_sharedstate A2_sharedstate;


/*
 * Generic flags for objects, stored in the userbits field of handles
 *
 *	NOTE: Only 8 bits! Other flag sets should stay away from these.
 */
typedef enum A2_handleflags
{
	A2_LOCKED =		0x01,
	A2_APIOWNED =		0x02,
	A2_HANDLEFLAGS =	0xff
} A2_handleflags;


/*---------------------------------------------------------
	Utilities
---------------------------------------------------------*/

/*
 * Dynamically allocated table of <name, handle>
 */
typedef struct A2_ntitem
{
	char		*name;
	A2_handle	handle;
} A2_ntitem;

typedef struct A2_nametab
{
	unsigned	size;
	unsigned	nitems;
	A2_ntitem	*items;
} A2_nametab;

/*
 * Add item to table, reallocating as needed, returning the index of the new
 * item, or a negative error code in case of failure.
 */
int a2nt_AddItem(A2_nametab *nt, const char *name, A2_handle h);

/* Find item by name. Return handle, or negative error code. */
A2_handle a2nt_FindItem(A2_nametab *nt, const char *name);

/* Find item by handle. Return index, or negative error code. */
int a2nt_FindItemByHandle(A2_nametab *nt, A2_handle h);

/* Free internal structures. Will NOT release any handles! */
void a2nt_Cleanup(A2_nametab *nt);


/*
 * Dynamically allocated table of handles
 */
typedef struct A2_handletab
{
	unsigned	size;
	unsigned	nitems;
	A2_handle	*items;
} A2_handletab;

/*
 * Add item to table, reallocating as needed, returning the index of the new
 * item, or a negative error code in case of failure.
 */
int a2ht_AddItem(A2_handletab *ht, A2_handle h);

/* Return the index of handle 'h', or -1 if 'h' is not in the table. */
int a2ht_FindItem(A2_handletab *ht, A2_handle h);

/* Free internal structures. Will NOT release any handles! */
void a2ht_Cleanup(A2_handletab *ht);


/*
 * Compare two timestamps. Returns (a - b) with correct handling of timestamp
 * wrapping.
 *
 * NOTE:
 *	This will assume that b is BEFORE a if the difference is more than half
 *	of the "wrap period"! This is to allow proper handling of messages that
 *	arrive late.
 */
static inline int a2_TSDiff(unsigned a, unsigned b)
{
	return (int)(a - b);
}


/*---------------------------------------------------------
	f20 numbers
FIXME: Drop these! It was a "fun hack" for 32 bit fixed size VM instructions.
---------------------------------------------------------*/

/* Convert f20 to int32 */
static inline int a2_f2i(int f)
{
	return f << 16 >> (f >> 16);
}

/* Convert int32 to f20 */
static inline int a2_i2f(int i)
{
	int x = 0;
	int n = 0;
	if(i < 0)
		n = 1,	i = -i;
	if(!(i & 0x7f800000))
		x = 8,	i <<= 8;
	if(!(i & 0x78000000))
		x += 4,	i <<= 4;
	if(!(i & 0x60000000))
		x += 2,	i <<= 2;
	if(!(i & 0x40000000))
		x += 1,	i <<= 1;
	if(n)
		i = -i;
	return ((unsigned) i >> 16) | (x << 16);
}


/*---------------------------------------------------------
	Configuration and drivers
---------------------------------------------------------*/

void a2_DumpConfig(A2_config *c);


/*---------------------------------------------------------
	VM and code generation
---------------------------------------------------------*/

/*
 * VM instructions.
 * NOTE: *R versions MUST be right after their non-R counterparts!
 *
 * NOTE: The END instruction, opcode 0, is left out here, as it needs special
 *       treatment in enums for safely portable code.
 */
#define A2_ALLINSTRUCTIONS						\
	/* Program flow control */					\
	/*A2_DI(END)*/	A2_DI(RETURN)	A2_DI(CALL)			\
									\
	/* Local flow control */					\
	A2_DI(JUMP)	A2_DI(LOOP)	A2_DI(JZ)	A2_DI(JNZ)	\
	A2_DI(JG)	A2_DI(JL)	A2_DI(JGE)	A2_DI(JLE)	\
									\
	/* Timing */							\
	A2_DI(DELAY)	A2_DI(DELAYR)	A2_DI(TDELAY)	A2_DI(TDELAYR)	\
									\
	/* Message handling */						\
	A2_DI(SLEEP)	A2_DI(WAKE)	A2_DI(FORCE)			\
									\
	/* Arithmetics */						\
	A2_DI(SUBR)	A2_DI(DIVR)	A2_DI(P2DR)	A2_DI(NEGR)	\
	A2_DI(LOAD)	A2_DI(LOADR)	A2_DI(ADD)	A2_DI(ADDR)	\
	A2_DI(MUL)	A2_DI(MULR)	A2_DI(MOD)	A2_DI(MODR)	\
	A2_DI(QUANT)	A2_DI(QUANTR)	A2_DI(RAND)	A2_DI(RANDR)	\
									\
	/* Unit control */						\
	A2_DI(SET)							\
									\
	/* Subvoice control */						\
	A2_DI(PUSH)	A2_DI(PUSHR)	A2_DI(SPAWN)	A2_DI(SPAWNR)	\
	A2_DI(SPAWND)	A2_DI(SPAWNDR)	A2_DI(SEND)	A2_DI(SENDA)	\
	A2_DI(SENDS)	A2_DI(WAIT)	A2_DI(KILL)	A2_DI(KILLA)	\
									\
	/* Debugging */							\
	A2_DI(DEBUG)	A2_DI(DEBUGR)					\
									\
	/* Special instructions */					\
	A2_DI(INITV)

#define	A2_DI(x)	OP_##x,
typedef enum A2_opcodes
{
	OP_END = 0,
	A2_ALLINSTRUCTIONS
	A2_OPCODES	/* Total number of VM opcodes */
} A2_opcodes;
#undef	A2_DI

/* First VM register that may have a write callback */
#define	A2_FIRSTCONTROLREG	A2_FIXEDREGS

void a2_DumpIns(unsigned *code, unsigned pc);


/*---------------------------------------------------------
	Stream interface internals
---------------------------------------------------------*/

/*
 * Stream interface/instance (see <audiality2/stream.h>)
 *
 * NOTE:
 *	Objects supporting the stream interface need to have a pointer to one
 *	of these as the FIRST field in their instance struct!
 */
struct A2_stream
{
	A2_state	*state;		/* State this stream belongs to */
	void		*object;	/* Object this stream belongs to */
	void		*streamdata;	/* Stream implementation data */
	A2_handle	handle;		/* Target object handle */
	unsigned	flags;		/* Stream init and state flags */
	unsigned	position;	/* Current stream position */

	/*
	 * a2_Read() backend. (Optional; operations will fail if not specified!)
	 */
	A2_errors (*Read)(A2_stream *str,
			A2_sampleformats fmt, void *buffer, unsigned size);

	/*
	 * a2_Write() backend. (Optional; operations will fail if not specified!)
	 */
	A2_errors (*Write)(A2_stream *str,
			A2_sampleformats fmt, const void *data, unsigned size);

	/*
	 * a2_[SG]etPos() backends. (Optional; the 'position' field is updated
	 * directly if no callback is specified.)
	 */
	A2_errors (*SetPos)(A2_stream *str, unsigned offset);
	unsigned (*GetPos)(A2_stream *str);

	/*
	 * a2_Flush() backend. (Optional; operations will do nothing and always
	 * suceed if this callback is not specified.)
	 */
	A2_errors (*Flush)(A2_stream *str);

	/*
	 * a2_StreamClose() backend. (Optional; if this callback is not
	 * specified, the Flush() callback, if specified, will be called
	 * instead.)
	 */
	A2_errors (*Close)(A2_stream *str);
};

typedef A2_errors (*A2_stropen_cb)(A2_stream *str);


/*---------------------------------------------------------
	Engine type registry
---------------------------------------------------------*/

struct A2_typeinfo
{
	/* Engine state (for destructors) */
	A2_state	*state;

	/*
	 * Open a stream on an object of this type. Leave this NULL if the
	 * stream interface is not supported!
	 */
	A2_stropen_cb	OpenStream;
};

A2_errors a2_RegisterType(A2_state *st, A2_otypes otype, const char *name,
		RCHM_destructor_cb destroy, A2_stropen_cb stropen);


/*---------------------------------------------------------
	Engine structures
---------------------------------------------------------*/

struct A2_string
{
	unsigned	length;		/* Length, not including terminator */
	char		*buffer;	/* Null terminated C string */
};

/*
 * Bank - container of objects
 */
struct A2_bank
{
	char		*name;

	/* Exported objects */
	A2_nametab	exports;

	/* Dependency table - internal and external objects */
	A2_handletab	deps;
};


/*
 * VM program, with voice structure definition and functions/message handlers
 */
typedef enum A2_iocodes
{
	A2_IO_MATCHOUT =	-1,	/* Match voice output count */
	A2_IO_WIREOUT =		-2,	/* MATCHOUT + autowire to voice out! */
	A2_IO_DEFAULT =		-3	/* Nothing specified in the script! */
} A2_iocodes;

typedef struct A2_structitem A2_structitem;
struct A2_structitem
{
	A2_structitem		*next;
	const A2_unitdesc	*unitdesc;
	unsigned		flags;		/* A2_unitflags */
	int16_t			ninputs;	/* Count or A2_iocodes! */
	int16_t			noutputs;	/* Count or A2_iocodes! */
#if 0
	uint8_t 		inputs[A2_MAXCHANNELS];
	uint8_t 		outputs[A2_MAXCHANNELS];
#endif
};

typedef struct A2_function
{
	unsigned	*code;		/* VM code */
	uint16_t	size;		/* Size of 'code' (instructions) */
	uint8_t		argv;		/* First register of argument list */
	uint8_t		argc;		/* Number of arguments */
	int		argdefs[A2_MAXARGS];	/* Argument default values */
} A2_function;

struct A2_program
{
	A2_function	*funcs;		/* Function and handler entry points */
	A2_structitem	*structure;	/* Voice structure definition */
	int8_t		eps[A2_MAXEPS];	/* Message to funcs index map */
	uint16_t	vflags;		/* Extra voice flags (A2_voiceflags) */
	int8_t		buffers;	/* Number of scratch buffers needed */
	uint8_t		nfuncs;		/* Number of local functions */
};

/*
 * VM call stack entry - essentially a backup of a VM state
 */
struct A2_stackentry
{
	A2_stackentry	*prev;
	A2_vstates	state;
	unsigned	timer;		/* Timer as message is handled */
	unsigned	pc;		/* PC of calling instruction */
	uint16_t	func;		/* Index of calling function */
	uint8_t		firstreg;	/* First register saved */
	uint8_t		interrupt;	/* Interrupt! (Timer hack.) */
	int		r[A2_REGISTERS - A2_CREGISTERS]; /* Saved registers */
};


/*
 * Internal event struct - sent directly to voice event queues
 */
typedef enum A2_evactions
{
	/* API to engine messages */
	A2MT_PLAY = 0,	/* a2_Play() */
	A2MT_START,	/* a2_Start() */
	A2MT_SEND,	/* a2_Send() (also an internal event) */
	A2MT_SENDSUB,	/* a2_SendSub() */
	A2MT_RELEASE,	/* A2_TVOICE handle destructor */
	A2MT_KILL,	/* a2_Kill() */
	A2MT_KILLSUB,	/* a2_KillSub() */

	/* Engine to API messages */
	A2MT_DETACH,	/* Free handle if rc 0, otherwise make it A2_TDETACHED */
	A2MT_ERROR,	/* Error message from the engine */
} A2_evactions;

typedef struct A2_eventbody
{
	/* WARNING: Check api.c after reordering these in any way! */
	uint16_t	action;		/* Message action code */
	uint16_t	argc;		/* Argument count */
	unsigned	timestamp;	/* When to process (frames, 24:8 fixp) */
	int		a1;		/* Handle, index etc */
	int		a2;		/* Second handle, index etc */
	int		a[A2_MAXARGS];	/* Arguments (16:16 fixp) */
} A2_eventbody;

struct A2_event
{
	A2_event	*next;		/* Next event en queue */
	A2_eventbody	b;
	NUMMSGS(unsigned number;)
	MSGTRACK(const char *source;)
};

typedef enum A2_voiceflags
{
	A2_SUBINLINE =	0x0100,		/* Subvoices as inline unit */
	A2_ATTACHED =	0x0200		/* Voice attached to handle or parent */
} A2_voiceflags;

/* Voice - node of the processing tree graph */
struct A2_voice
{
	A2_voice	*next;		/* Next voice in list */
	A2_event	*events;	/* Event queue */
	A2_stackentry	*stack;		/* VM call stack */

	/* VM state */
	A2_program	*program;	/* Currently executing VM program */
	A2_vmstate	s;		/* Control, special and work regs */

	A2_handle	handle;		/* Handle, if wired to the API */
	uint16_t	flags;		/* A2_voiceflags */
	uint8_t		nestlevel;	/* Nest level, for scratch buffers */

	/* Units and control registers */
	uint8_t		cregisters;	/* Number of potentially wired regs */
/*FIXME: Remove the reserved registers from here? */
	A2_write_cb	cwrite[A2_REGISTERS];	/* Write callbacks for cregs */
	A2_unit		*cunit[A2_REGISTERS];	/* Unit instance for cregs */
	A2_unit		*units;			/* Chain of voice units */

	/* Sub-voices */
	A2_voice	*sub;			/* List of all subvoices */
	A2_voice	*sv[A2_REGISTERS];	/* Attached sub-voices */

	unsigned	noutputs;
	int32_t		**outputs;
};

/* Audio bus */
struct A2_bus
{
	unsigned	channels;	/* Number of channels allocated */
	int32_t		*buffers[A2_MAXCHANNELS];
};

/* Block - allocation unit for A2_stackentry and mixing buffers */
typedef union A2_block A2_block;
union A2_block
{
	A2_block	*next;			/* Free list link */
	A2_stackentry	stackentry;		/* VM stack entry */
	A2_unit		unit;			/* Voice unit instance */
	int32_t		buffer[A2_MAXFRAG];	/* Audio buffer */
	A2_bus		bus;			/* Audio bus */
};

/* State resources that are shared by master states and substates */
struct A2_sharedstate
{
	RCHM_manager	hm;		/* Handle manager */
	A2_program	*terminator;	/* Dummy program for killed voices */
	A2_compiler	*c;		/* A2S compiler */
	char		strbuf[A2_TMPSTRINGSIZE]; /* For API return strings */
};

/* Audiality 2 state */
struct A2_state
{
	A2_state	*parent;	/* Parent state, if substate */
	A2_state	*next;		/* First or next substate */
	A2_sharedstate	*ss;		/* Objects, banks etc */
	A2_handle	rootvoice;	/* Root voice of this (sub)state */

	A2_config	*config;	/* Current state configuration */
	A2_audiodriver	*audio;		/* Audio I/O driver */
	A2_sysdriver	*sys;		/* System interfaces */

/*FIXME: These should really be used with read/write barriers to be safe... */
	volatile unsigned now_frames;	/* Audio time of last cb (frames, 24:8) */
	volatile unsigned now_ticks;	/* Tick of last audio callback (ms) */
	volatile unsigned now_guard;	/* Guard, matching now_frames */

	unsigned	timestamp;	/* Current timestamp for async API */
	SFIFO		*fromapi;	/* Messages from async. API calls */
	SFIFO		*toapi;		/* Responses to the API context */

	A2_voice	*voicepool;	/* LIFO stack of voices */
	unsigned	totalvoices;	/* Number of voices in use + pool */
	unsigned	activevoices;	/* Number of voices in use */

	A2_block	*blockpool;	/* LIFO stack of memory blocks */
	A2_event	*eventpool;	/* LIFO stack of event structs */
	unsigned	now_fragstart;	/* For internal message timing */
	NUMMSGS(unsigned msgnum;)

	unsigned	msdur;		/* One millisecond in samples (16:16) */
	uint32_t	noisestate;	/* Noise generator state */

	int		statreset;	/* Flag to reset averaging/summing */
	uint64_t	now_micros;	/* Performance monitoring timestamp */
	uint64_t	avgstart;	/* Timestamp for averaging start */
	unsigned	instructions;	/* VM instruction counter */
	unsigned	cputimesum;	/* Time spent in audio callback */
	unsigned	cputimecount;	/* Number of audio callbacks */
	unsigned	cputimeavg;	/* Average time spent in callback */
	unsigned	cputimemax;	/* Maximum time spent in audio cb */
	unsigned	cpuloadmax;	/* Maximum CPU load */
	unsigned	cpuloadavg;	/* Average CPU load */

	/* Global audio buffers */
	A2_bus		*master;		/* Master outputs */
	A2_bus		*scratch[A2_NESTLIMIT];	/* Intermediate buffers */
};


/*---------------------------------------------------------
	Object/handle management
---------------------------------------------------------*/

A2_errors a2_RegisterBankTypes(A2_state *st);

static inline A2_bank *a2_GetBank(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi || (hi->typecode != A2_TBANK))
		return NULL;
	return (A2_bank *)hi->d.data;
}

static inline A2_program *a2_GetProgram(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi || (hi->typecode != A2_TPROGRAM))
		return NULL;
	return (A2_program *)hi->d.data;
}

static inline A2_unitdesc *a2_GetUnit(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi || (hi->typecode != A2_TUNIT))
		return NULL;
	return (A2_unitdesc *)hi->d.data;
}

static inline A2_voice *a2_GetVoice(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi || (hi->typecode != A2_TVOICE))
		return NULL;
	return (A2_voice *)hi->d.data;
}

/* Brutally kill all voices. (Dirty hack for unloading objects...) */
void a2_InstaKillAllVoices(A2_state *st);


/*---------------------------------------------------------
	Realtime block memory manager
---------------------------------------------------------*/

static inline A2_block *a2_NewBlock(A2_state *st)
{
	A2_block *b = st->sys->RTAlloc(st->sys, sizeof(A2_block));
	if(!b)
		return NULL;
#ifdef DEBUG
	fprintf(stderr, "Audiality 2: Block pool exhausted!"
			"Allocated new block %p.\n", b);
#endif
	return b;
}

static inline A2_block *a2_AllocBlock(A2_state *st)
{
	A2_block *b = st->blockpool;
	if(b)
		st->blockpool = b->next;
	else
		b = a2_NewBlock(st);
	return b;
}

static inline void a2_FreeBlock(A2_state *st, void *block)
{
	((A2_block *)block)->next = st->blockpool;
	st->blockpool = (A2_block *)block;
}


/*---------------------------------------------------------
	Bus
---------------------------------------------------------*/

/* Allocate a new bus with the specified number of channels */
static inline A2_bus *a2_AllocBus(A2_state *st, unsigned channels)
{
	int i;
	A2_bus *b = (A2_bus *)a2_AllocBlock(st);
	if(!b)
		return NULL;
	b->channels = channels;
	for(i = 0; i < channels; ++i)
		if(!(b->buffers[i] = (int32_t *)a2_AllocBlock(st)))
		{
			while(--i >= 0)
				a2_FreeBlock(st, b->buffers[i]);
			a2_FreeBlock(st, b);
			return NULL;
		}
	return b;
}

/* Ensure that an existing bus has at least the specified number of channels */
static inline int a2_ReallocBus(A2_state *st, A2_bus *bus, unsigned channels)
{
	for( ; bus->channels < channels; ++bus->channels)
		if(!(bus->buffers[bus->channels] = (int32_t *)a2_AllocBlock(st)))
			return 0;
	return 1;
}

/* Clear the specified subfragment of all channels of the specified bus */
static inline void a2_ClearBus(A2_bus *bus, unsigned offset, unsigned frames)
{
	int i;
	for(i = 0; i < bus->channels; ++i)
		memset(bus->buffers[i] + offset, 0, sizeof(int32_t) * frames);
}

/* Free a bus, including any buffers it may be using */
static inline void a2_FreeBus(A2_state *st, A2_bus *bus)
{
	int i;
	for(i = 0; i < bus->channels; ++i)
		a2_FreeBlock(st, bus->buffers[i]);
	a2_FreeBlock(st, bus);
}


/*---------------------------------------------------------
	Voice event handling
---------------------------------------------------------*/

static inline A2_event *a2_NewEvent(A2_state *st)
{
	A2_event *e = st->sys->RTAlloc(st->sys, sizeof(A2_event));
	if(!e)
		return NULL;
#ifdef DEBUG
	fprintf(stderr, "Audiality 2: Event pool exhausted!"
			"Allocated new event %p.\n", e);
#endif
	return e;
}

static inline A2_event *a2_AllocEvent(A2_state *st)
{
	A2_event *e = st->eventpool;
	if(e)
		st->eventpool = e->next;
	else
		e = a2_NewEvent(st);
	NUMMSGS(e->number = st->msgnum++;)
	MSGTRACK(e->source = "unknown";)
	return e;
}

static inline void a2_FreeEvent(A2_state *st, A2_event *e)
{
	e->next = st->eventpool;
	st->eventpool = e;
}

static inline void a2_SendEvent(A2_voice *v, A2_event *e)
{
	A2_event *pe = v->events;
	if(!pe || (a2_TSDiff(pe->b.timestamp, e->b.timestamp) > 0))
	{
		e->next = pe;
		v->events = e;
	}
	else
	{
		while(pe->next && (a2_TSDiff(pe->next->b.timestamp,
				e->b.timestamp) <= 0))
			pe = pe->next;
		e->next = pe->next;
		pe->next = e;
	}
}


/*---------------------------------------------------------
	Voice management
---------------------------------------------------------*/

A2_voice *a2_VoiceAlloc(A2_state *st);
A2_errors a2_init_root_voice(A2_state *st);
A2_voice *a2_VoiceNew(A2_state *st, A2_voice *parent);
A2_errors a2_VoiceStart(A2_state *st, A2_voice *v,
		A2_program *p, int argc, int *argv);
A2_errors a2_VoiceCall(A2_state *st, A2_voice *v, unsigned func,
		int argc, int *argv, int interrupt);
void a2_VoiceKill(A2_state *st, A2_voice *v);
void a2_VoiceFree(A2_state *st, A2_voice **head);

static inline void a2_VoiceDetach(A2_voice *v)
{
	v->flags &= ~A2_ATTACHED;
	if(v->s.state >= A2_ENDING)
		v->s.timer = 0;	/* Wake up and terminate! */
}

/*
 * Process a linked list of voices, recursively processing any subvoices that
 * are not handled by 'inline' units.
 */
void a2_ProcessVoices(A2_state *st, A2_voice **head, unsigned offset,
		unsigned frames);

/* Get the A2_voice that the specified A2_vmstate belongs to */
static inline A2_voice *a2_voice_from_vms(A2_vmstate *vms)
{
	return (A2_voice *)((char *)vms - offsetof(A2_voice, s));
}


/*---------------------------------------------------------
	Internal DSP callbacks
---------------------------------------------------------*/

/* Process callbacks for the 'inline' unit */
void a2_inline_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames);
void a2_inline_Process(A2_unit *u, unsigned offset, unsigned frames);

/* Audio driver callback - this is what drives the whole engine! */
A2_errors a2_AudioCallback(A2_audiodriver *driver, unsigned frames);


/*---------------------------------------------------------
	Waves
---------------------------------------------------------*/

A2_errors a2_InitWaves(A2_state *st, A2_handle bank);
A2_errors a2_RegisterWaveTypes(A2_state *st);


/*---------------------------------------------------------
	Async API message gateway
---------------------------------------------------------*/

A2_errors a2_OpenAPI(A2_state *st);
A2_errors a2_RegisterAPITypes(A2_state *st);
A2_errors a2r_PumpEngineMessages(A2_state *st);
A2_errors a2_PumpAPIMessages(A2_state *st);
void a2_CloseAPI(A2_state *st);

void a2r_DetachHandle(A2_state *st, A2_handle h);


/*---------------------------------------------------------
	Error handling
---------------------------------------------------------*/

/* Last error code for "top level" API calls */
extern A2_errors a2_last_error;

/* Send an error message from an engine context to its API state. */
A2_errors a2r_Error(A2_state *st, A2_errors e, const char *info);


/*---------------------------------------------------------
	Timing
---------------------------------------------------------*/

#ifdef _WIN32
extern DWORD a2_start_time;
extern LARGE_INTEGER a2_perfc_frequency = 0;
#else
extern struct timeval a2_start_time;
#endif

static inline uint32_t a2_GetTicks(void)
{
#ifdef _WIN32
	DWORD now;
	now = timeGetTime();
	if(now < a2_start_time)
		return ((~(DWORD)0) - a2_start_time) + now;
	else
		return now - a2_start_time;
#else
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - a2_start_time.tv_sec) * 1000 +
			(now.tv_usec - a2_start_time.tv_usec) / 1000;
#endif
}

static inline uint64_t a2_GetMicros(void)
{
#ifdef _WIN32
	LARGE_INTEGER now;
	if(!a2_perfc_frequency || !QueryPerformanceCounter(&now))
		return (LARGE_INTEGER)a2_GetTicks() * 1000;
	return now * 1000000 / a2_perfc_frequency;
#else
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - a2_start_time.tv_sec) * 1000000.0f +
			(now.tv_usec - a2_start_time.tv_usec);
#endif
}

#endif /* A2_INTERNALS_H */
