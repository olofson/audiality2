/*
 * a2_properties.h - Audiality 2 property interface
 *
 * Copyright 2010-2017 David Olofson <david@olofson.net>
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

#ifndef A2_PROPERTIES_H
#define A2_PROPERTIES_H

#include "a2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------
	Object property interface
---------------------------------------------------------*/

typedef enum A2_properties
{
	/*
	 * General properties (most objects)
	 */
	A2_PGENERAL =		0x00010000,

	A2_PCHANNELS,		/* Number of channels */
	A2_PFLAGS,		/* Flags */
	A2_PREFCOUNT,		/* Reference count of the handle */

	/*
	 * Size, position, buffer status, for waves, streams etc. Units depend
	 * on the target object, but are typically sample frames.
	 */
	A2_PSIZE,		/* Total size of object */
	A2_PPOSITION,		/* Current read or write position */
	A2_PAVAILABLE,		/* Items currently available for reading */
	A2_PSPACE,		/* Space currently available for writing */

	/*
	 * Global settings (state)
	 */
	A2_PSTATE =		0x00020000,

	A2_PSAMPLERATE,		/* Audio I/O sample rate */
	A2_PBUFFER,		/* Audio I/O buffer size */
	A2_PTIMESTAMPMARGIN,	/* Timestamp jitter margin delay (ms) */
	A2_PTABSIZE,		/* Tab size for script position printouts */
	A2_POFFLINEBUFFER,	/* Buffer size for offline rendering */
	A2_PSILENCELEVEL,	/* Max peak level considered as silence */
	A2_PSILENCEWINDOW,	/* Rolling window size for silence detection */
	A2_PSILENCEGRACE,	/* Grace period before considering silence */
	A2_PRANDSEED,		/* 'rand' instruction RNG seed/state */
	A2_PNOISESEED,		/* 'wtosc' noise generator seed/state */
	A2_LOGLEVELS,		/* Loglevel (bit mask) */

	/*
	 * Statistics (state)
	 */
	A2_PSTATISTICS =	0x00030000,

	A2_PACTIVEVOICES,	/* Number of active voices */
	A2_PACTIVEVOICESMAX,	/* Peak number of active voices */
	A2_PFREEVOICES,		/* Number of voices in pool */
	A2_PTOTALVOICES,	/* Number of voices in total */
	A2_PCPULOADAVG,		/* Average DSP CPU load (%) */
	A2_PCPULOADMAX,		/* Peak DSP CPU load (%) */
	A2_PCPUTIMEAVG,		/* Average buffer processing time (ms) */
	A2_PCPUTIMEMAX,		/* Peak buffer processing time (ms) */
	A2_PINSTRUCTIONS,	/* VM instructions executed */

	A2_PAPIMESSAGES,	/* Number of API messages received */
	A2_PTSMARGINAVG,	/* Timestamp deadline margin; average */
	A2_PTSMARGINMIN,	/* Timestamp deadline margin; minimum */
	A2_PTSMARGINMAX		/* Timestamp deadline margin; maximum */

} A2_properties;

A2_errors a2_GetProperty(A2_interface *i, A2_handle h, A2_properties p,
		int *v);
A2_errors a2_SetProperty(A2_interface *i, A2_handle h, A2_properties p, int v);

typedef struct A2_property {
	A2_properties	property;
	int		value;
} A2_property;

A2_errors a2_SetProperties(A2_interface *i, A2_handle h, A2_property *props);

A2_errors a2_GetStateProperty(A2_interface *i, A2_properties p, int *v);
A2_errors a2_SetStateProperty(A2_interface *i, A2_properties p, int v);
A2_errors a2_SetStateProperties(A2_interface *i, A2_property *props);

#if 0
/* TODO: */
/*
 * Gets a range of properties. 'property' fields should be set by the caller,
 * and the 'value' fields will be filled in by the engine.
 */
A2_errors a2_GetProperties(A2_interface *i, A2_handle h, A2_property *props);

/*
 * Asynchronously request a set of properties from the engine. This call will
 * never block.
 * TODO: Message system for receiving responses to these.
 */
A2_errors a2_RequestProperties(A2_interface *i, A2_handle h,
		A2_properties *props);
#endif

#ifdef __cplusplus
};
#endif

#endif /* A2_PROPERTIES_H */
