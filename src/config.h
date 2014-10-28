/*
 * config.h - Audiality 2 compile time configuration
 *
 * Copyright 2010-2014 David Olofson <david@olofson.net>
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

#ifndef A2_CONFIG_H
#define A2_CONFIG_H

#ifdef DEBUG
#	include	<stdio.h>
#	include	<assert.h>
#	define	DBG(x)		x	/* General debug output */
#	define	NUMMSGS(x)		/* Message order tracking */
#	define	MSGTRACK(x)		/* Track origin of messages */
#	define	EVLEAKTRACK(x)		/* Check for event leaks */
#	define	DUMPMSGS(x)		/* Dump messages from audio context */
#	define	DUMPCODE(x)		/* Enable compiler VM code output */
#	define	DUMPSOURCE(x)	x	/* Dump source lines while compiling */
#	define	SYMBOLDBG(x)		/* Compiler symbol table debugging */
#	define	REGDBG(x)		/* Register allocation debugging */
#	define	DUMPLSTRINGS(x)		/* Lexer string processing output */
#	define	DUMPSTRUCT(x)		/* Compiler voice structure dumping */
#	define	DUMPSTRUCTRT(x)		/* Realtime voice structure dumping */
#	define	DUMPCODERT(x)		/* Enable realtime VM code dumping */
#	define	DUMPSIZES(x)		/* Dump engine struct sizes at init */
#	undef	CERRDIE			/* Die in assert(0) on compile errs */
#	undef	DUMPTOKENS		/* Enable lexer token printout */
#	define	THROWSOURCE		/* a2c_Throw() prints <file>:<line> */
#else
#	define	DBG(x)
#	define	NUMMSGS(x)
#	define	MSGTRACK(x)
#	define	EVLEAKTRACK(x)
#	define	DUMPMSGS(x)
#	define	DUMPCODE(x)
#	define	DUMPSOURCE(x)
#	define	SYMBOLDBG(x)
#	define	REGDBG(x)
#	define	DUMPLSTRINGS(x)
#	define	DUMPSTRUCT(x)
#	define	DUMPSTRUCTRT(x)
#	define	DUMPCODERT(x)
#	define	DUMPSIZES(x)
#	undef	CERRDIE
#	undef	DUMPTOKENS
#	undef	THROWSOURCE
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

/* Quality for wavetable oscillators */
#define	A2_HIFI
#undef	A2_LOFI

/* Default tick duration; corresponds to 'tempo 120 4' */
#define	A2_DEFAULTTICK		(125 << 16)

/*
 * Max number of VM instructions a voice VM is allowed to run without
 * processing samples. If exceeded, the voice will be killed, to avoid freezing
 * the whole sound engine.
 */
#define A2_INSLIMIT		1000

/*
 * Maximum allowed child voice nesting depth. (Recursive explosion inhibitor.)
 */
#define	A2_NESTLIMIT		255

/* Default initial pool sizes for A2_REALTIME states */
#define	A2_INITHANDLES		256
#define	A2_INITVOICES		256
#define	A2_INITBLOCKS		512

/* Size of temporary string buffers (bytes) */
#define	A2_TMPSTRINGSIZE	256

#endif /* A2_CONFIG_H */
