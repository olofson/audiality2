/*
 * a2_drivers.h - Audiality 2 device driver and configuration interfaces
 *
 * Copyright 2012-2017, 2022 David Olofson <david@olofson.net>
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

#ifndef A2_DRIVERS_H
#define A2_DRIVERS_H

#include "a2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum A2_drivertypes
{
	A2_ANYDRIVER = 0,	/* Any driver type */
	A2_SYSDRIVER,		/* System driver (memory manager etc) */
	A2_AUDIODRIVER,		/* Audio I/O driver */
	A2_MIDIDRIVER		/* MIDI I/O driver */
} A2_drivertypes;


/*---------------------------------------------------------
	Configuration
---------------------------------------------------------*/

/* Configuration struct for a2_Open() and a2_SubState() */
struct A2_config
{
	/* Parameters */
	A2_interface	*interface;	/* Master interface of state using
					 * this config, if any */
	A2_driver	*drivers;	/* Linked list of attached drivers */
	int		samplerate;	/* Audio sample rate (Hz) */
	int		buffer;		/* I/O buffer size (sample frames) */
	int		channels;	/* Number of audio channels */
	int		flags;		/* Init and state flags */
	int		poolsize;	/* Main memory pool size (bytes) */
	int		blockpool;	/* Initial block pool size */
	int		voicepool;	/* Initial voice pool size */
	int		eventpool;	/* Initial event pool size */

	/* Information (read-only; valid only after a2_Open()!) */
	float		basepitch;	/* Middle C pitch (1.0/oct) */
};

/*
 * Create a configuration, initialized to sensible defaults based on the
 * arguments. Arguments set to -1 are interpreted as default values, currently;
 *	samplerate	48000
 *	buffer		1024
 *	channels	2
 *	flags		0
 *
 * If no drivers are added, default drivers will be instantiated as needed when
 * an engine state is opened. An application may add builtin drivers retrieved
 * with a2_GetDriver(), or custom drivers provided by the application. This
 * way, applications can interface Audiality 2 with practically any API, engine
 * or environment.
 *
 * NOTE:
 *	If left 0 (default), blockpool, voicepool, and eventpool are set to
 *	"reasonable" defaults automatically by a2_Open().
 *
 *	Also, if a realtime audio driver is used, a2_Open() automatically
 *	transfers the A2_REALTIME flag to the configuration. Applications
 *	should only set the A2_REALTIME flag when using a normally
 *	non-realtime driver, such as 'buffer', and calling a2_Run() from
 *	another thread.
 *
 * NOTE:
 *	The returned A2_config must be closed using a2_CloseConfig(), unless
 *	the A2_AUTOCLOSE flag is used, and the config is used with a2_Open().
 */
A2_config *a2_OpenConfig(int samplerate, int buffer, int channels, int flags);

/*
 * Add a driver to a configuration.
 *
 * NOTE:
 *	This will happily add any number of drivers of any type, completely
 *	disregarding common sense and what may already be in the configuration!
 */
A2_errors a2_AddDriver(A2_config *config, A2_driver *driver);

/*
 * Get the last added driver of the specified type from a configuration. If no
 * such driver exists in the configuration, a default driver of the specified
 * type will be instantiated, added and returned automatically.
 */
A2_driver *a2_GetDriver(A2_config *config, A2_drivertypes type);

/*
 * Open any drivers of a configuration that aren't already open. Drivers opened
 * by this call will have 'flags' added to their 'flags' fields.
 */
A2_errors a2_OpenDrivers(A2_config *config, int flags);

/*
 * Close any open drivers of a configuration. If 'mask' is non-zero, only
 * drivers with the corresponding bits set in their flag fields will be
 * considered.
 */
A2_errors a2_CloseDrivers(A2_config *config, int mask);

/* Close 'config', closing and destroying any attached drivers. */
void a2_CloseConfig(A2_config *config);

/*
 * Create an instance of a built-in driver. NULL will return the default or
 * prefered driver of the specified type on the system at hand.
 *
 * NOTE:
 *	This creates driver instances! One must call the Destroy() method to
 *	destroy these, releasing resources properly.
 *
 * NOTE:
 *	If Audiality 2 is built without drivers, these calls will always return
 *	NULL, and all system interfaces must be provided by the applications.
 */
A2_driver *a2_NewDriver(A2_drivertypes type, const char *name);


/*---------------------------------------------------------
	Driver API
---------------------------------------------------------*/

/* Common driver interface (used as header in actual driver structs!) */
struct A2_driver
{
	A2_driver	*next;		/* Next driver in chain */
	A2_config	*config;	/* Configuration (parent object) */
	A2_drivertypes	type;		/* Type of this driver */
	const char	*name;

	int		flags;		/* Initialization and status flags */

	int		optc;		/* Option count */
	const char	**optv;		/* Options */

	/* Open/Close */
	A2_errors (*Open)(A2_driver *driver);
	void (*Close)(A2_driver *driver);

	/* Destroy this driver instance! */
	void (*Destroy)(A2_driver *driver);

	/* (Implementation specific data may follow) */
};

/*
 * Open the specified driver, unless already open, and if successfully opened,
 * add 'flags' to the driver's flags field.
 */
A2_errors a2_OpenDriver(A2_driver *driver, int flags);

/*
 * Close the specified driver, if open.
 */
void a2_CloseDriver(A2_driver *driver);


/*---------------------------------------------------------
	Driver registry
---------------------------------------------------------*/

/* Callback for creating a driver instance */
typedef A2_driver *(*A2_newdriver_cb)(A2_drivertypes type,
		const char *nameopts);

/* Opaque pointer representing a registered driver */
typedef struct A2_regdriver A2_regdriver;

/* Add a driver to the driver registry. */
A2_errors a2_RegisterDriver(A2_drivertypes type, const char *name,
		A2_newdriver_cb create);

/*
 * Remove the specified driver from the registry. NULL unregisters all drivers,
 * built-in ones included.
 *
 * Returns A2_NOTFOUND if no driver of the specified type, by the specified
 * name, exist in the registry.
 */
A2_errors a2_UnregisterDriver(const char *name);

/* Unregisters all drivers and re-registers the built-in drivers, if any. */
void a2_ResetDriverRegistry(void);

/*
 * Find a driver in the driver registry, and create an instance. A comma
 * delimited list of options can be appended after the actual driver name, and
 * will be parsed and added to optc/optv of the returned driver struct.
 */
A2_driver *a2_NewDriver(A2_drivertypes type, const char *nameopts);

/*
 * Destroy a driver instance as returned from a2_NewDriver(). If the driver is
 * open, it will be closed automatically.
 */
void a2_DestroyDriver(A2_driver *driver);

/*
 * Find registered driver. If 'type' is A2_ANYDRIVER, all types of drivers can
 * be returned. If 'prev' is NULL, the first matching driver in the registry is
 * returned.
 *
 * Returns an opaque driver registry entry pointer, or NULL, if no matching
 * driver was found.
 */
A2_regdriver *a2_FindDriver(A2_drivertypes type, A2_regdriver *prev);

/* Get name of driver */
const char *a2_DriverName(A2_regdriver *driver);

/* Get type of driver */
A2_drivertypes a2_DriverType(A2_regdriver *driver);

/* Get descriptive name of driver type code */
const char *a2_DriverTypeName(A2_drivertypes dt);


/*---------------------------------------------------------
	Builtin drivers
---------------------------------------------------------*/

/* Public interface for A2_SYSDRIVER */
typedef struct A2_sysdriver A2_sysdriver;
struct A2_sysdriver
{
	A2_driver	driver;

	/*
	 * Realtime memory manager
	 *
	 * NOTE:
	 *	While these calls are primarily intended for realtime memory
	 *	management within the state it's wired to, they are also used
	 *	during initialization and destruction of that context. However,
	 *	there will be no concurrent calls within the engine state.
	 */
	void *(*RTAlloc)(A2_sysdriver *driver, unsigned size);
	void (*RTFree)(A2_sysdriver *driver, void *block);
/*
TODO:	void *(*APIAlloc)(A2_sysdriver *driver, unsigned size);
TODO:	void (*APIFree)(A2_sysdriver *driver, void *block);

TODO:	void (*RTFreeFromAPI)(A2_sysdriver *driver, void *block);
TODO:	void (*APIFreeFromRT)(A2_sysdriver *driver, void *block);
*/
	/* (Implementation specific data may follow) */
};


/*
 * Public interface for A2_AUDIODRIVER
 *
 * IMPORTANT:
 *	If the Run() entry point is not NULL, it must be called once per buffer
 *	for the driver to actually do anything! (Typical audio I/O drivers rely
 *	on background threads or callbacks from the host system instead.)
 *
 * NOTE:
 *	A few API calls need API/engine locking. In some cases, this may cause
 *	the engine to skip processing while the lock is held, rather than
 *	actually blocking the realtime context until the lock is released!
 */
typedef struct A2_audiodriver A2_audiodriver;
struct A2_audiodriver
{
	A2_driver	driver;

	/* Operation */
	A2_errors (*Run)(A2_audiodriver *driver, unsigned frames);

	/* Locking for synchronous API operations */
	void (*Lock)(A2_audiodriver *driver);
	void (*Unlock)(A2_audiodriver *driver);

	/* Engine interface */
	void		*state;		/* State data for Process() */
	void (*Process)(A2_audiodriver *driver, unsigned frames);
	float		**buffers;	/* Array of audio buffers */

	/* (Implementation specific data may follow) */
};

/*
 * Public interface for A2_MIDIDRIVER
 *
 *	The Connect() call is used for wiring MIDI channels to voices, which
 *	will receive MIDI events on the specified MIDI channel, translated to
 *	voice messages. Connecting to voice 0 will disconnect the voice, and
 *	subsequent messages on the MIDI channel will be dropped.
 *
 *	The Poll() entry point will be called from the engine context whenever
 *	MIDI events are to be delivered to the engine, typically at the start
 *	of each engine audio callback.
 *
 *	Timing of the Poll() calls well depend on the audio I/O configuration
 *	of the engine context, which means that for accurate, low jitter MIDI
 *	timing, a MIDI driver should either use a timestamping MIDI API, or
 *	interface with the MIDI API via a dedicated high priority thread.
 */
typedef struct A2_mididriver A2_mididriver;
struct A2_mididriver
{
	A2_driver	driver;

	/* Operation */
	A2_errors (*Connect)(A2_mididriver *driver, int channel,
			A2_handle voice);
	A2_errors (*Poll)(A2_mididriver *driver, unsigned frames);

	/* (Implementation specific data may follow) */
};


/*
 * Wire MIDI input to a MIDI handler voice. Events from the specified MIDI
 * channel are sent as messages to entry point 7 of the specified voice.
 *
 * 'driver' is the driver to connect, and needs to be of type A2_MIDIDRIVER. If
 * NULL is passed, the first open MIDI driver is used.
 *
 * 'channel' specifies the MIDI channel (0..15) to connect. If it is -1, all
 * available channels are connected to the voice.
 *
 * The argument list is (Msg Ch Arg1 Arg2), where 'Msg' is the MIDI message
 * type (NOTEOFF, NOTEON etc in midi.a2s), and 'Ch' is the MIDI channel. 'Arg1'
 * and 'Arg2' are used (or unused) depending on 'Msg';
 *
 *	Msg		Arg1			Arg2
 *	NOTEOFF		Note pitch		Release velocity
 *	NOTEON		Note pitch		Velocity
 *	AFTERTOUCH	Note pitch		Pressure
 *	CONTROLCHANGE	Control (see midi.a2s)	Value
 *	PROGRAMCHANGE	Program (0..127)	<unused>
 *	CHANNELPRESSURE	Pressure		<unused>
 *	PITCHBEND	Amount			<unused>
 *	RPN		Parameter (0..16343)	Value
 *	NRPN		Parameter (0..16343)	Value
 *
 *		Note pitch is corresponds to MIDI pitch, where 60 is middle C.
 *		Subtract 60 and divide by 12 to convert to A2 linear pitch.
 *
 *		Velocities, control values etc, are transformed to real values
 *		in the [0, 1] range, except for PITCHBEND, which is [-1, 1].
 *
 *		SYSTEM messages are not supported, and will not be passed on
 *		to MIDI handlers.
 *
 * See midi.a2s for MIDI messages and CC definitions, and miditest.a2s for an
 * example of how to use this interface.
 */
A2_errors a2_MIDIHandler(A2_interface *i, A2_driver *driver, int channel,
		A2_handle voice);

#ifdef __cplusplus
};
#endif

#endif /* A2_DRIVERS_H */
