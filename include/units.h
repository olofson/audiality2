/*
 * units.h - Audiality 2 Voice Unit API
 *
 * Copyright (C) 2010-2012 David Olofson <david@olofson.net>
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

/*
 * General note on "real time safeness" and performance optimization:
 *
 *	Since most of the code of a Audiality 2 unit will normally run within the
 *	realtime audio processing context of the engine, it is of utmost
 *	importance that the code is designed and optimized appropriately. In
 *	short, this means:
 *
 *		* No blocking or potentially time consuming system calls!
 *
 *		* Code should be optimized for deterministic execution times,
 *		  rather than best average throughput!
 *
 *		* Design and code for speed! Keep in mind that many of the
 *		  callbacks may be called thousads of times per second during
 *		  normal operation.
 *
 *		* Minimize memory and cache footprint! A sound engine running
 *		  in a low latency realtime setting typically wakes up about a
 *		  thousand times per second, and goes through all voices, VMs
 *		  and units at least once every time. Large data structures and
 *		  poor memory access patterns can have a massive performance
 *		  impact. The impact is even greater on a system under heavy
 *		  load, since other processes tend to push the sound engine out
 *		  of the cache whenever it goes to sleep after finishing a
 *		  buffer cycle.
 */

#ifndef A2_UNITS_H
#define A2_UNITS_H

#include "vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct A2_unitdesc A2_unitdesc;
typedef struct A2_unit A2_unit;
typedef struct A2_crdesc A2_crdesc;

typedef enum A2_unitflags
{
	A2_PROCADD =		0x00000001,	/* Adding Process() */
} A2_unitflags;

/*
 * Control register write callback for A2_cregdesc
 *
 *	If 'frames' is non-zero, controls that support ramping should set up a
 *	linear ramp from the current value to 'value' over 'frames' sample
 *	frames.
 *	   Ramping is normally performed with sample accurate timing (by means
 *	of "buffer splitting"), but asynchronous (typically long) ramps may not
 *	always terminate exactly on a buffer boundary! It is acceptable for
 *	units to handle this by stretching the last segment of the ramp to the
 *	full length of the processing fragment the ramp should end in. This is
 *	still fairly accurate, as processing fragment size is restricted to
 *	A2_MAXFRAGMENT (normally 32 sample frames) for cache footprint reasons.
 *
 *	'frames' is just an target point for ramping, so it may be greater than
 *	A2_MAXFRAGMENT. Do not rely on it being directly related to buffer
 *	sizes!
 *
 * NOTE:
 *	This will run in the real time context of Audiality 2, and will be called
 *	whenever a VM program changes the contents of the respective control
 *	register!
 */
typedef void (*A2_write_cb)(A2_unit *u, A2_vmstate *vms, int value, int frames);

/*
 * Initialization callback
 *
 *	This MANDATORY callback is used for initializing unit instances. The
 *	instance memory block 'u' is allocated by Audiality 2 (size guaranteed to
 *	be at least 'instancesize' as specified in the A2_unitdesc), and the
 *	A2_unit header portion will be filled in before the initializer is
 *	called.
 *
 * NOTE:
 *	This will me called whenever a voice using the unit is instantiated,
 *	and this happens on they fly, in the realtime audio context. Keep it
 *	fast and deterministic!
 */
typedef A2_errors (*A2_uinit_cb)(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags);

/*
 * Deinitialization callback
 *
 *	This OPTIONAL callback can be used to release any resources external to
 *	the A2_unit block as a unit is destroyed. (The A2_unit block is freed by
 *	Audiality 2.) This normally happens whenever a voice terminates.
 */
typedef void (*A2_udeinit_cb)(A2_unit *u, A2_state *st);

/*
 * Process callback for A2_unit
 *
 *	This MANDATORY callback is where the actual audio processing normally
 *	happens.
 *
 *	Since Audiality 2 is using "buffer splitting" for sample accurate timing,
 *	processing is done in variable size fragments. The 'offset' argument
 *	specifies where in the connected I/O buffers processing is to begin, and
 *	'frames' specifies how many sample frames to process from that point on.
 *
 *	'frames' will never be greater than A2_MAXFRAGMENT.
 */
typedef void (*A2_process_cb)(A2_unit *u, unsigned offset, unsigned frames);


/*
 * Control register descriptor. (End array with { NULL, NULL }!
 *
 *	This specifies the name identifying a unit control register in CSL, and
 *	optionally provides a callback that notifies the unit about changes to
 *	the register value, along with ramping information.
 *
 *	Readable control registers may be implemented by having the unit write
 *	back values into the VM registers via the 'registers' pointer in the
 *	A2_unit header of the unit instance.
 *
 * NOTE:
 *	It is actually legal to specify control registers without callbacks!
 *	What happens is that a VM register is allocated, but the register works
 *	like a normal, "passive" VM register. The unit is expected to read the
 *	register value via A2_vmstate when desired.
 */
struct A2_crdesc
{
	const char	*name;		/* Register name for the compiler */
	A2_write_cb	write;		/* Callback to write control register */
};


/* Unit descriptor */
struct A2_unitdesc
{
	const char	*name;		/* Unit name for struct definitions */

	/* Control */
	const A2_crdesc	*registers;	/* Array of register descriptors */

	/* Audio I/O */
	uint8_t		mininputs;	/* Minimum number of inputs */
	uint8_t		maxinputs;	/* Maximum number of inputs */
	uint8_t		minoutputs;	/* Minimum number of outputs */
	uint8_t		maxoutputs;	/* Maximum number of outputs */

	/* Instantiation */
	unsigned	instancesize;	/* Size of A2_unit instances (bytes) */
	A2_uinit_cb	Initialize;
	A2_udeinit_cb	Deinitialize;
};


/*
 * Unit instance
 *
 *	Initialized by the host:
 *		next
 *		ninputs, inputs
 *		noutputs, outputs
 *		registers
 *		Deinitialize
 *
 *	To be initialized by the A2_unitdesc Initialize() callback:
 *		flags
 *		Process
 */
struct A2_unit
{
	A2_unit			*next;
	const A2_unitdesc	*descriptor;

	/* Audio I/O */
	uint16_t	ninputs;	/* Number of input channels */
	uint16_t	noutputs;	/* Number of output channels */
	int32_t		**inputs;	/* Ptrs to arrays of buffer pointers */
	int32_t		**outputs;

	/* Control */
	int		*registers;

	/* Processing */
	A2_process_cb	Process;

	/* (Implementation specific details may follow) */
};


/*
 * Register a voice unit class.
 *
 * NOTE:
 *	The unit is not exported or tied to any bank! This needs to be done
 *	explictly by other means for the unit to actually be made available to
 *	scripts.
 *
 * NOTE:
 *	Due to complexity and performance considerations, we're not doing
 *	reference counting on the realtime engine side at this point, so we
 *	just lock these in place! The official way of unloading units is to
 *	close the engine state.
 */
A2_handle a2_RegisterUnit(A2_state *st, const A2_unitdesc *ud);

#ifdef __cplusplus
};
#endif

#endif	/* A2_UNITS_H */
