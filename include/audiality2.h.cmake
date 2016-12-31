/*
 * audiality2.h - Audiality 2 Realtime Scriptable Audio Engine
 *
 * Copyright 2010-2016 David Olofson <david@olofson.net>
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

#ifndef AUDIALITY2_H
#define AUDIALITY2_H

#include "a2_interface.h"
#include "a2_drivers.h"
#include "a2_properties.h"
#include "a2_waves.h"
#include "a2_pitch.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* Versioning */
#define	A2_MAKE_VERSION(major, minor, micro, build)	\
		(((major) << 24) | ((minor) << 16) | ((micro) << 8) | (build))
#define	A2_MAJOR(ver)	(((ver) >> 24) & 0xff)
#define	A2_MINOR(ver)	(((ver) >> 16) & 0xff)
#define	A2_MICRO(ver)	(((ver) >> 8) & 0xff)
#define	A2_BUILD(ver)	((ver) & 0xff)

/* Current version */
#define	A2_VERSION	A2_MAKE_VERSION(@VERSION_MAJOR@, @VERSION_MINOR@, @VERSION_PATCH@, @VERSION_BUILD@)

/* Maximum number of sample frames to process at a time */
#define	A2_MAXFRAG		64

/* Minimum size of the blocks allocated by a2_AllocBlock() */
#define	A2_BLOCK_SIZE		384

/* Maximum number of audio channels supported */
#define	A2_MAXCHANNELS		8

/* Default seed for 'rand' instruction pseudo-random number generator */
#define	A2_DEFAULT_RANDSEED	16576

/* Default seed for 'noise' wave pseudo-random number generator */
#define	A2_DEFAULT_NOISESEED	324357


/*---------------------------------------------------------
	Error handling
---------------------------------------------------------*/

/*
 * Return the last error code set by a2_Open(), a2_SubState(), a2_OpenConfig(),
 * a2_AddDriver(), a2_GetDriver(), a2_OpenDrivers() or a2_NewDriver().
 *
 * NOTE: This does NOT reset the error code! The aforementioned calls do.
 */
A2_errors a2_LastError(void);

/*
 * Return and reset the last error code sent to interface 'i'.
 */
A2_errors a2_LastRTError(A2_interface *i);

/* Return textual explanation of a Audiality 2 error code */
const char *a2_ErrorString(A2_errors errorcode);

/* Return the name or description of the specified error code */
const char *a2_ErrorName(A2_errors errorcode);
const char *a2_ErrorDescription(A2_errors errorcode);


/*---------------------------------------------------------
	Versioning
---------------------------------------------------------*/

/* Return version of the Audiality 2 headers the application was built with. */
static inline unsigned a2_HeaderVersion(void)
{
	return A2_VERSION;
}

/* Return version of the linked Audiality 2 library. */
unsigned a2_LinkedVersion(void);


/*---------------------------------------------------------
	Engine state and interface management
---------------------------------------------------------*/

/*
 * Create an Audiality state using the provided configuration. If NULL is
 * specified, a default configuration is created.
 * 
 * If a driver in a provided configuration is already open, the 'samplerate',
 * 'buffer', 'channels' and 'flags' arguments are ignored, and the
 * corresponding values are instead retrieved from the driver. In this case,
 * the driver will NOT be closed with the state, unless the application sets
 * the A2_AUTOCLOSE flag in the driver's 'flag' field.
 *
 * Returns the master interface to the state, with timestamping and context
 * behavior configured according to the specified 'config'.
 *
 * NOTE:
 *	The 'flags' argument is only passed on to the driver 'flags' field when
 *	a driver is opened by the state! That is, flags are not passed on to a
 *	driver that is already open when a2_Open() is called.
 */
A2_interface *a2_Open(A2_config *config);

/*
 * Create a substate to the state behind interface 'master'.
 *
 * The substate shares waves, programs and other objects with the master state.
 * Making API calls that create or manipulate such on a substate is equivalent
 * to operating directly on the substate's master state.
 *
 * The substate has its own engine context, with its own set of groups and
 * voices, independent from and asynchronous to those of the substate's master
 * state. Realtime control API calls on a substate operate on this local engine
 * context, allowing the substate to perform realtime or offline processing
 * independent of the master state.
 *
 * Returns the master interface to the substate, with timestamping and context
 * behavior configured according to the specified 'config'.
 *
 * NOTE:
 *	Substates are NOT reentrant/thread safe in relation to each other, or
 *	their master states! To safely perform background rendering in another
 *	thread or similar, a separate master state must be used.
 */
A2_interface *a2_SubState(A2_interface *master, A2_config *config);

/*
 * Acquire an interface of the sort indicated by 'flags';
 *
 *	A2_REALTIME	Interface for use in the context of the realtime audio
 *			thread/callback, as needed for use from within driver,
 *			unit, or stream callback code. That is, an interface
 *			that operates directly on the engine state, with no
 *			buffering or synchronization.
 *
 *			If this flag is not specified, the returned interface
 *			will be configured for use from the API context.
 *
 *			For non realtime engine states, such as ones set up for
 *			off-line rendering, this flag has no effect; interfaces
 *			will always operate directly on the engine state.
 *
 *	A2_TIMESTAMP	Enable timestamping in the play/control API of the
 *			interface. (This guarantees a unique interface
 *			instance, as the timestamping API is stateful.)
 *
 *	A2_NOREF	The new interface will not count as a reference to the
 *			underlying engine state, and as a result, the engine
 *			state will be closed once all other interfaces have
 *			been closed, leaving this interface detached.
 *
 *			A2_NOREF suitable for creating interfaces that should
 *			not lock the engine state in place, but there is a risk
 *			of code attempting to use or close the interface after
 *			the engine state has been closed.
 *
 *	A2_AUTOCLOSE	Like A2_NOREF, but instead of being left detached, the
 *			interface created will be be closed automatically when
 *			the engine state is closed.
 *
 *			A2_AUTOCLOSE is suitable when it can be guaranteed that
 *			the interface will not be accessed after the engine
 *			state has been closed.
 *
 * NOTE:
 *	Interfaces are reference counted, and all interfaces of an Audiality
 *	state need to be closed in order to close the state!
 */
A2_interface *a2_Interface(A2_interface *master, int flags);

/*
 * Close an Audiality interface. If the interface is the last interface to its
 * parent state or substate, the (sub)state is closed as well.
 *
 * NOTE:
 *	Substates CAN be closed manually, but if they aren't, they are closed
 *	automatically as their master state is closed.
 */
void a2_Close(A2_interface *i);


/*---------------------------------------------------------
	Handle management
---------------------------------------------------------*/

/*
 * Hardcoded handles
 */
#define	A2_ROOTBANK	0


/*
 * Returns the handle of the root voice of the (sub)state behind the specified
 * interface.
 *
 * NOTE:
 *	While substates share banks, waves, programs etc with their parent
 *	states, all in the same handle space, they have their own voices - and
 *	these must not be mixed up! Bad Things(TM) will happen if you talk to
 *	a state about voices that belong to another state...
 */
A2_handle a2_RootVoice(A2_interface *i);


/*
 * General handle operations
 */

/*
 * Return type of object with 'handle', or a negated error code if 'handle' is
 * invalid, or the operation failed for other reasons.
 */
A2_otypes a2_TypeOf(A2_interface *i, A2_handle handle);

/* Return name string of 'type'. */
const char *a2_TypeName(A2_interface *i, A2_otypes typecode);

/* Return real value representation of the object assigned to 'handle' */
double a2_Value(A2_interface *i, A2_handle handle);

/* Return a string representation of the object assigned to 'handle' */
const char *a2_String(A2_interface *i, A2_handle handle);

/* Return the name of the object assigned to 'handle', if any is defined */
const char *a2_Name(A2_interface *i, A2_handle handle);

/*
 * Returns the size of the object assigned to 'handle', or a negated error
 * code if the operation failed, or isn't applicable to the object.
 */
int a2_Size(A2_interface *i, A2_handle handle);

/*
 * Attempt to increase the reference count of 'handle' by one.
 */
A2_errors a2_Retain(A2_interface *i, A2_handle handle);

/*
 * Decrease the reference count of 'handle' by one. If the reference count
 * reaches zero, the handle will be released, and (typically) the associated
 * object is destroyed.
 *
 * Returns 0 (A2_OK) if the object actually is released. Otherwise, an error
 * code is returned, most commonly A2_REFUSE, as a result of the object
 * intentionally refusing to destruct.
 * 
 * NOTE:
 *	Voices will return A2_REFUSE here, as they need a roundtrip to the
 *	engine context before the handle can safely be returned to the pool!
 *
 *	Also note that when dealing with objects referenced by timestamped
 *	messages, it's important to use a2_Release() with the right interface,
 *	as handles may otherwise be invalid by the time those messages are
 *	processed.
 */
static inline A2_errors a2_Release(A2_interface *i, A2_handle handle)
{
	return i->Release(i, handle);
}

/*
 * Have 'owner' claim ownership of 'handle'.
 *
 * NOTE:
 *	Only certain object types can claim ownership of other objects!
 *
 * NOTE:
 *	This does NOT increase the reference count of 'handle'! The logic is
 *	that the caller owns the object, and hands it over to 'owner'.
 */
A2_errors a2_Assign(A2_interface *i, A2_handle owner, A2_handle handle);

/*
 * Have 'owner' claim ownership of 'handle' and add it to 'owner's exports as
 * 'name'. If 'name' is NULL, an attempt is made at getting a name from
 * a2_Name().
 */
A2_errors a2_Export(A2_interface *i, A2_handle owner, A2_handle handle,
		const char *name);


/*---------------------------------------------------------
	Object loading/creation
---------------------------------------------------------*/

/*
 * Create a new, empty bank. 'name' is the import name for scripts to use;
 * NULL results in a unique name being generated automatically.
 */
A2_handle a2_NewBank(A2_interface *i, const char *name, int flags);

/*
 * Load .a2s file 'fn' or null terminated string 'code' as a bank.
 *
 * a2_Load() will normally try to find an already loaded bank with the
 * specified name, before attempting to locate, load and compile it. To always
 * load a new instance of the specified bank, use the A2_NOSHARED flag.
 *
 * Returns the handle of the resulting bank, or if the operation fails, a
 * negative error code. (Use (-result) to get the A2_errors code.)
 */
A2_handle a2_LoadString(A2_interface *i, const char *code, const char *name);
A2_handle a2_Load(A2_interface *i, const char *fn, unsigned flags);

/*
 * Create a constant object of 'value'. Returns the handle of the constant
 * object, or a negative error code.
 */
A2_handle a2_NewConstant(A2_interface *i, double value);

/*
 * Create a string object from the null terminated 'string'. Returns the handle
 * of the string object, or a negative error code.
 */
A2_handle a2_NewString(A2_interface *i, const char *string);

/*
 * Decreases the reference count of all objects that have been created as
 * direct results of API calls.
 *
 * Returns the number of objects released, not including recursive side
 * effects.
 *
 * NOTE:
 *	This call should generally NOT be used by applications that manage
 *	objects explicitly! It still affects objects after a2_Retain() has been
 *	used on them.
 */
int a2_UnloadAll(A2_interface *i);


/*---------------------------------------------------------
	Offline rendering
---------------------------------------------------------*/

/*
 * Run a state (or substate) that's using a driver without a thread or similar
 * context of its own, that is, one that implements the Run() method. Typically
 * the "buffer" driver is used for this, and this is the default driver for
 * states created with a2_SubState().
 *
 * NOTE:
 *	With an A2_REALTIME state, this call does not need to be made from the
 *	API context! It essentially replaces the realtime callback of a normal
 *	audio API driver.
 *
 * Returns the number of sample frames (not bytes!) actually rendered, or a
 * negated A2_errors error code.
 */
int a2_Run(A2_interface *i, unsigned frames);

/*
 * Run 'program' off-line with the specified arguments, rendering at
 * 'samplerate', writing the output to 'stream'.
 * 
 * Rendering will stop after 'length' sample frames have been rendered, or if
 * 'length' is 0, when the output is silent.
 *
 * Returns number of sample frames rendered, or a negated A2_errors error code.
 */
int a2_Render(A2_interface *i,
		A2_handle stream,
		unsigned samplerate, unsigned length, A2_property *props,
		A2_handle program, unsigned argc, int *argv);


/*---------------------------------------------------------
	Objects and exports
---------------------------------------------------------*/

/*
 * Return handle of object specified by 'path' relative to object 'node'.
 * Object names are separated with '/' characters.
 *
 * NOTE:
 *	This call does NOT distinguish between private and exported symbols!
 *	It will return any matching object, whether or not it is exported.
 *
 * Returns a negative A2_errors error code if no object was found.
 */
A2_handle a2_Get(A2_interface *i, A2_handle node, const char *path);

/*
 * Get handle of export 'x' of object 'node'. Positive (including zero) indexes
 * address exported symbols, while negative indexes address private symbols.
 *
 * Returns -A2_WRONGTYPE if 'node' cannot have exports, or -A2_INDEXRANGE if
 * 'x' is out of range.
 */
A2_handle a2_GetExport(A2_interface *i, A2_handle node, int x);

/*
 * Get name of export 'x' of object 'node'. Positive (including zero) indexes
 * address exported symbols, while negative indexes address private symbols.
 *
 * Returns NULL if 'node' cannot have exports, or if 'x' is out of range.
 */
const char *a2_GetExportName(A2_interface *i, A2_handle node, int x);


/*---------------------------------------------------------
	Background processing
---------------------------------------------------------*/

/*
 * Due to the lock-free nature of Audiality 2, there are asynchronous jobs that
 * need to be performed in the API context. This is done by running the API
 * message pump inside API calls that typically deal with timestamped
 * messages, namely:
 *		a2_Start*()
 *		a2_Play*()
 *		a2_Send*()
 *		a2_SendSub*()
 *		a2_Kill()
 *		a2_KillSub()
 *
 * If none of the above are called regularly, the application should call
 * a2_PumpMessages(), to explicitly process API messages.
 *
 * a2_Release(), a2_*Callback(), a2_OpenSink(), a2_OpenSource() and other calls
 * may pump API messages as well in certain situations, but that is not to be
 * relied upon in any way.
 */
void a2_PumpMessages(A2_interface *i);


/*---------------------------------------------------------
	Callback xinsert interface
---------------------------------------------------------*/

/*
 * Callback prototype for a2_SinkCallback(), a2_SourceCallback() and
 * a2_InsertCallback().
 *
 * This will be called with (NULL, 0, 0, <userdata>) as notification when the
 * callback is removed/replaced, or the 'x*' unit is destroyed.
 */
typedef A2_errors (*A2_xinsert_cb)(int32_t **buffers, unsigned nbuffers,
		unsigned frames, void *userdata);

/*
 * a2_SinkCallback(), a2_SourceCallback() and a2_InsertCallback() are used for
 * setting up callbacks to tap, inject, and process audio, respectively. The
 * callbacks will be called by the Process() method of the first unit that
 * supports this mechanism, found in the specified voice. These callbacks will
 * never be called with a 'frames' argument greater than A2_MAXFRAG.
 *
 * The root voice, and groups created with a2_NewGroup(), have an 'xinsert'
 * unit last in their voice structures, so they support this API by default. To
 * use these functions with any other voice, the voice needs to run a program
 * that includes an 'xsink', 'xsource' or 'xinsert' unit somewhere in its
 * structure, or the functions will fail with A2_NOXINSERT.
 *
 * If there are multiple clients, they all receive the same audio, and their
 * output is summed, as applicable. That is, xinsert clients always run in
 * parallel, rather than being chained.
 *
 * These functions return an xinsert client handle if the operation was
 * successful, or a negated error code such as:
 *	-A2_EXPUNIT	voice has no 'x*' units
 *	-A2_NOXINSERT	no 'x*' unit found
 *	-A2_BADVOICE	'voice' is not the handle of a voice
 */

/*
 * Set up 'callback' to receive audio from the first 'xsink' or 'xinsert' unit
 * of 'voice'.
 */
A2_handle a2_SinkCallback(A2_interface *i, A2_handle voice,
		A2_xinsert_cb callback, void *userdata);

/*
 * Set up 'callback' to feed audio into the first 'xsource' or 'xinsert' unit
 * of 'voice'. The callback will receive write-only buffers (undefined
 * contents!), and the audio in these buffers will be mixed into the output of
 * the 'xsource' or 'xinsert' unit.
 */
A2_handle a2_SourceCallback(A2_interface *i, A2_handle voice,
		A2_xinsert_cb callback, void *userdata);

/*
 * Essentially a2_SinkCallback() and a2_SourceCallback() rolled into one; the
 * equivalent of an insert jack on a studio mixing console. This function will
 * only work with 'xinsert' units, as it expects both input and output. The
 * callback will receive buffers with audio from the 'xinsert' unit inputs, and
 * whatever these buffers contain when the callback returns is mixed into the
 * respective outputs of the unit. This is essentially a quick and dirty way of
 * implementing custom DSP effects without implementing voice units.
 */
A2_handle a2_InsertCallback(A2_interface *i, A2_handle voice,
		A2_xinsert_cb callback, void *userdata);


/*---------------------------------------------------------
	Buffered stream xinsert interface
---------------------------------------------------------*/

/*
 * NOTE: This interface doesn't quite match the callback API, as the stream
 *       API isn't really designed for read/write streams. You *can* use
 *       a2_OpenSink() along with a2_OpenSource() to implement something
 *       logically similar to a2_InsertCallback(), but unlike the latter,
 *       asynchronous streams cannot allow "zero latency" insert processing.
 */

/*
 * Open a buffered asynchronous stream for receiving audio from 'channel' of
 * the first 'xsink' or 'xinsert' unit on 'voice'. 'size' is the stream buffer
 * size in sample frames.
 *
TODO:
 * Specifying -1 for 'channel' opens all available channels on 'voice', which
 * can then be read from the stream by specifying the appropriate interleaved
 * sample format to a2_Read(). (See types.h; A2_sampleformats.)
 *
 * Returns a stream handle for use with a2_Read(), or a negated error code.
 */
A2_handle a2_OpenSink(A2_interface *i, A2_handle voice,
		int channel, int size, unsigned flags);

/*
 * Open a buffered asynchronous stream for injecting audio into 'channel' of
 * the first 'xsource' or 'xinsert' unit on 'voice'. 'size' is the stream
 * buffer size in sample frames.
 *
TODO:
 * Specifying -1 for 'channel' opens all available channels on 'voice', which
 * can then be written to by specifying the appropriate interleaved sample
 * format to a2_Write(). (See types.h; A2_sampleformats.)
 *
 * Returns a stream handle for use with a2_Write(), or a negated error code.
 */
A2_handle a2_OpenSource(A2_interface *i, A2_handle voice,
		int channel, int size, unsigned flags);


/*---------------------------------------------------------
	Utilities
---------------------------------------------------------*/

/* Return pseudo-random number in the range [0, max[ */
float a2_Rand(A2_interface *i, float max);

/* Returns the number of milliseconds elapsed since A2 API initialization */
unsigned a2_GetTicks(void);

/*
 * Attempt to sleep for 'milliseconds', letting go of the CPU. Returns the
 * number of milliseconds actually slept.
 */
unsigned a2_Sleep(unsigned milliseconds);

/* Dump VM assembly code of the specified object, where applicable */
A2_errors a2_DumpCode(A2_interface *i, A2_handle h, FILE *stream,
		const char *prefix);

/*TODO*/
/* Calculate size of converted data */
int a2_ConvertSize(A2_interface *i, A2_sampleformats infmt,
		A2_sampleformats outfmt, unsigned size);

/*TODO*/
/* Convert audio data from one format to another */
A2_errors a2_Convert(A2_interface *i,
		A2_sampleformats infmt, const void *indata, unsigned insize,
		A2_sampleformats outfmt, void *outdata, unsigned flags);

#ifdef __cplusplus
};
#endif

#endif /* AUDIALITY2_H */
