/*
 * a2_vm.h - Public Audiality 2 VM declarations
 *
 * Copyright 2010-2012, 2015-2016 David Olofson <david@olofson.net>
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

#ifndef A2_VM_H
#define A2_VM_H

#include "audiality2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of VM registers */
#define	A2_REGISTERS	32

/* Maximum number of arguments to a VM program or function */
#define	A2_MAXARGS	8

/* Maximum number of entry points a VM program can have. (EP 0 is "main()".) */
#define	A2_MAXEPS	8

/* Voice and messages */
typedef enum A2_vstates
{
	A2_RUNNING = 0,
	A2_WAITING,	/* In a *DELAY instruction or similar state */
	A2_INTERRUPT,	/* Message handler context */
	A2_ENDING,	/* In top-level RETURN, ready for destruction */
	A2_FINALIZING	/* Detached, waiting for subvoices to finish */
} A2_vstates;

/* Hardwired control registers */
typedef enum A2_cregisters
{
	R_TICK = 0,
	R_TRANSPOSE,
	A2_CREGISTERS
} A2_cregisters;

#define	A2_FIXEDREGS	A2_CREGISTERS

/* Public VM state data (needed by some voice units) */
typedef struct A2_vmstate
{
	unsigned	waketime;	/* Wakeup time (frames, 24:8 fixp) */
	uint8_t		state;		/* Current state */
	uint8_t		func;		/* Current function index */
	uint16_t	pc;		/* PC of calling instruction */
	int		r[A2_REGISTERS];	/* VM registers */
}  A2_vmstate;

#ifdef __cplusplus
};
#endif

#endif /* A2_VM_H */
