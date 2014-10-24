/*
 * types.h - Audiality 2 basic data types
 *
 * Copyright 2012-2014 David Olofson <david@olofson.net>
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

#ifndef A2_TYPES_H
#define A2_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif


/*---------------------------------------------------------
	Various widely used Audiality 2 types
---------------------------------------------------------*/

typedef	int A2_handle;
typedef struct A2_driver A2_driver;
typedef struct A2_config A2_config;
typedef struct A2_state A2_state;

/* Object types (Also used for RCHM handle type tagging!) */
typedef enum A2_otypes
{
	/* Semi-static, API managed objects */
	A2_TBANK = 1,	/* Bank of waves, programs etc */
	A2_TWAVE,	/* Sampled mipmapped wave for oscillators like wtosc */
	A2_TPROGRAM,	/* Instrument/sound/song program (compiled script) */
	A2_TUNIT,	/* Voice structure unit ("class" - not instance!) */
	A2_TSTRING,	/* Simple C string */
	A2_TSTREAM,	/* Audio stream (see stream.h) */
	A2_TXICLIENT,	/* xinsert client (stream target or callback) */
	A2_TDETACHED,	/* Former realtime handle that has been detached */

	/* Realtime engine managed objects (data pointers not accessible!) */
	A2_TNEWVOICE,	/* Virtual (not yet instantiated) voice */
	A2_TVOICE,	/* Playing voice instance */
} A2_otypes;

/* Sample formats for wave uploading, stream I/O etc */
typedef enum A2_sampleformats
{
	/* Sample formats */
	A2_I8 = 0,	/* [-128, 127] */
	A2_I16,		/* [-32768, 32767] */
	A2_I24,		/* [-8388608, 8388607] */
	A2_I32,		/* [-2147483648, 2147483647] */
	A2_F32,		/* [-1.0f, 1.0f] (Range irrelevant /w A2_NORMALIZE) */

	/* Channel counts for interleaved buffers */
	A2_INTERLEAVED_2 = 0x200,
	A2_INTERLEAVED_3 = 0x300,
	A2_INTERLEAVED_4 = 0x400,
	A2_INTERLEAVED_5 = 0x500,
	A2_INTERLEAVED_6 = 0x600,
	A2_INTERLEAVED_7 = 0x700,
	A2_INTERLEAVED_8 = 0x800
} A2_sampleformats;

#define A2_SF_FORMAT_MASK	0x0000000f
#define A2_SF_INTERLEAVE_MASK	0x00000f00


/*---------------------------------------------------------
	NULL (stolen from the GNU C Library)
---------------------------------------------------------*/
#ifndef NULL
#	if defined __GNUG__ &&					\
			(__GNUC__ > 2 ||			\
			(__GNUC__ == 2 && __GNUC_MINOR__ >= 8))
#		define NULL (__null)
#	else
#		if !defined(__cplusplus)
#			define NULL ((void*)0)
#		else
#			define NULL (0)
#		endif
#	endif
#endif


/*---------------------------------------------------------
	Error handling
---------------------------------------------------------*/

#define A2_ALLERRORS	\
/* === NOTE: These first codes should match RCHM_errors! === */\
  A2_DEFERR(A2_REFUSE,		"Destruction refused")\
  A2_DEFERR(A2_OOMEMORY,	"Out of memory")\
  A2_DEFERR(A2_OOHANDLES,	"Out of handles")\
  A2_DEFERR(A2_INVALIDHANDLE,	"Invalid handle")\
  A2_DEFERR(A2_FREEHANDLE,	"Handle already returned to the free pool")\
/* ========================================================= */\
  A2_DEFERR(A2_DEADHANDLE,	"Released (not locked) handle used by API")\
  A2_DEFERR(A2_END,		"VM program ended normally")\
  A2_DEFERR(A2_OVERLOAD,	"VM overload; too many instructions back-to-back")\
  A2_DEFERR(A2_ILLEGALOP,	"Illegal VM opcode")\
  A2_DEFERR(A2_LATEMESSAGE,	"API message arrived late to engine context")\
  A2_DEFERR(A2_MANYARGS,	"Too many arguments to VM program")\
  \
  A2_DEFERR(A2_BUFOVERFLOW,	"Buffer overflow")\
  A2_DEFERR(A2_BUFUNDERFLOW,	"Buffer underflow")\
  A2_DEFERR(A2_DIVBYZERO,	"Division by zero")\
  A2_DEFERR(A2_INFLOOP,		"Jump would cause infinite loop")\
  A2_DEFERR(A2_OVERFLOW,	"Value does not fit in numeric type")\
  A2_DEFERR(A2_UNDERFLOW,	"Value too small; would truncate to zero")\
  A2_DEFERR(A2_VALUERANGE,	"Value out of range")\
  A2_DEFERR(A2_INDEXRANGE,	"Index out of range")\
  A2_DEFERR(A2_OUTOFREGS,	"Out of VM registers")\
  \
  A2_DEFERR(A2_NOTIMPLEMENTED,	"Operation or feature not implemented")\
  A2_DEFERR(A2_OPEN,		"Error opening file")\
  A2_DEFERR(A2_NODRIVER,	"No driver of the required type available")\
  A2_DEFERR(A2_DRIVERNOTFOUND,	"Specified driver not found")\
  A2_DEFERR(A2_DEVICEOPEN,	"Error opening device")\
  A2_DEFERR(A2_ALREADYOPEN,	"Device is already open")\
  A2_DEFERR(A2_ISASSIGNED,	"Object is already assigned to this container")\
  A2_DEFERR(A2_READ,		"Error reading file or stream")\
  A2_DEFERR(A2_WRITE,		"Error writing file or stream")\
  A2_DEFERR(A2_READONLY,	"Object is read-only")\
  A2_DEFERR(A2_WRITEONLY,	"Object is write-only")\
  A2_DEFERR(A2_STREAMCLOSED,	"Stream closed by the other party")\
  A2_DEFERR(A2_WRONGTYPE,	"Wrong type of data or object")\
  A2_DEFERR(A2_WRONGFORMAT,	"Wrong stream data format")\
  A2_DEFERR(A2_VOICEALLOC,	"Could not allocate voice")\
  A2_DEFERR(A2_VOICEINIT,	"Could not initialize voice")\
  A2_DEFERR(A2_VOICENEST,	"Subvoice nesting depth exceeded")\
  A2_DEFERR(A2_IODONTMATCH,	"Input and output counts don't match")\
  A2_DEFERR(A2_FEWCHANNELS,	"Voice has to few channels for unit")\
  A2_DEFERR(A2_UNITINIT,	"Could not initialize unit instance")\
  A2_DEFERR(A2_NOTFOUND,	"Object not found")\
  A2_DEFERR(A2_NOOBJECT,	"Handle is not attached to an object")\
  A2_DEFERR(A2_NOXINSERT,	"No 'xinsert' unit found in voice structure")\
  A2_DEFERR(A2_NOSTREAMCLIENT,	"'xinsert' client not set up for streaming")\
  A2_DEFERR(A2_NOREPLACE,	"Unit does not implement replacing output mode")\
  A2_DEFERR(A2_NOTOUTPUT,	"Tried to wire inputs to voice output bus")\
  A2_DEFERR(A2_EXPORTDECL,	"Export already declared")\
  A2_DEFERR(A2_SYMBOLDEF,	"Symbol already defined")\
  A2_DEFERR(A2_UNDEFSYM,	"Undefined symbols in program")\
  A2_DEFERR(A2_MESSAGEDEF,	"Handler for this message already defined")\
  A2_DEFERR(A2_ONLYLOCAL,	"Symbols can only be local in this scope")\
  A2_DEFERR(A2_DECLNOINIT,	"Declared variable not initialized")\
  \
  A2_DEFERR(A2_EXPEOS,		"Expected end of statement")\
  A2_DEFERR(A2_EXPCLOSE,	"Expected closing brace")\
  A2_DEFERR(A2_EXPNAME,		"Expected name")\
  A2_DEFERR(A2_EXPVALUE,	"Expected value")\
  A2_DEFERR(A2_EXPVALUEHANDLE,	"Expected value or handle")\
  A2_DEFERR(A2_EXPINTEGER,	"Expected integer value")\
  A2_DEFERR(A2_EXPSTRING,	"Expected string literal")\
  A2_DEFERR(A2_EXPVARIABLE,	"Expected variable")\
  A2_DEFERR(A2_EXPLABEL,	"Expected label")\
  A2_DEFERR(A2_EXPPROGRAM,	"Expected program")\
  A2_DEFERR(A2_EXPFUNCTION,	"Expected function declaration")\
  A2_DEFERR(A2_EXPUNIT,		"Expected unit")\
  A2_DEFERR(A2_EXPBODY,		"Expected body")\
  A2_DEFERR(A2_EXPOP,		"Expected operator")\
  A2_DEFERR(A2_EXPBINOP,	"Expected binary operator")\
  A2_DEFERR(A2_EXPCONSTANT,	"Expected constant")\
  A2_DEFERR(A2_EXPWAVETYPE,	"Expected wave type identifier")\
  A2_DEFERR(A2_EXPEXPRESSION,	"Expected expression")\
  \
  A2_DEFERR(A2_NEXPEOF,		"Unexpected end of file")\
  A2_DEFERR(A2_NEXPNAME,	"Undefined symbol")\
  A2_DEFERR(A2_NEXPVALUE,	"Value not expected here")\
  A2_DEFERR(A2_NEXPHANDLE,	"Handle not expected here")\
  A2_DEFERR(A2_NEXPTOKEN,	"Unexpected token")\
  A2_DEFERR(A2_NEXPELSE,	"'else' not applicable here")\
  A2_DEFERR(A2_NEXPLABEL,	"Label not expected here")\
  \
  A2_DEFERR(A2_BADFORMAT,	"Bad file or device I/O format")\
  A2_DEFERR(A2_BADTYPE,		"Invalid type ID")\
  A2_DEFERR(A2_BADBANK,		"Invalid bank handle")\
  A2_DEFERR(A2_BADWAVE,		"Invalid waveform handle")\
  A2_DEFERR(A2_BADPROGRAM,	"Invalid program handle")\
  A2_DEFERR(A2_BADENTRY,	"Invalid program entry point")\
  A2_DEFERR(A2_BADVOICE,	"Voice does not exist, or bad voice id")\
  A2_DEFERR(A2_BADLABEL,	"Bad label name")\
  A2_DEFERR(A2_BADVALUE,	"Bad value")\
  A2_DEFERR(A2_BADJUMP,		"Illegal jump target position")\
  A2_DEFERR(A2_BADOPCODE,	"Invalid VM opcode")\
  A2_DEFERR(A2_BADREGISTER,	"Invalid VM register index")\
  A2_DEFERR(A2_BADREG2,		"Invalid VM register index, second argument")\
  A2_DEFERR(A2_BADIMMARG,	"Immediate argument out of range")\
  A2_DEFERR(A2_BADVARDECL,	"Variable cannot be declared here")\
  A2_DEFERR(A2_BADOCTESCAPE,	"Bad octal escape format in string literal")\
  A2_DEFERR(A2_BADDECESCAPE,	"Bad decimal escape format in string literal")\
  A2_DEFERR(A2_BADHEXESCAPE,	"Bad hex escape format in string literal")\
  \
  A2_DEFERR(A2_CANTEXPORT,	"Cannot export from this scope")\
  A2_DEFERR(A2_CANTINPUT,	"Unit cannot have inputs")\
  A2_DEFERR(A2_CANTOUTPUT,	"Unit cannot have outputs")\
  A2_DEFERR(A2_NOPROGHERE,	"Program cannot be declared here")\
  A2_DEFERR(A2_NOMSGHERE,	"Message cannot be declared here")\
  A2_DEFERR(A2_NOFUNCHERE,	"Function cannot be declared here")\
  A2_DEFERR(A2_NOTUNARY,	"Not a unary operator")\
  A2_DEFERR(A2_NOCODE,		"Code not allowed here")\
  A2_DEFERR(A2_NOTIMING,	"Timing instructions not allowed here")\
  A2_DEFERR(A2_NORUN,		"Cannot run program from here")\
  A2_DEFERR(A2_NORETURN,	"'return' not allowed in this context")\
  A2_DEFERR(A2_NOEXPORT,	"Cannot export this kind of symbol")\
  A2_DEFERR(A2_NOWAKEFORCE,	"'wake' and 'force' not applicable here")\
  A2_DEFERR(A2_NOPORT,		"Port is unavailable or does not exist")\
  A2_DEFERR(A2_NOINPUT,		"Unit with inputs where no audio is available")\
  A2_DEFERR(A2_NONAME,		"Object has no name")\
  A2_DEFERR(A2_NOUNITS,		"Voice has no units")\
  A2_DEFERR(A2_MULTIINLINE,	"Voice cannot have multiple inline units")\
  A2_DEFERR(A2_CHAINMISMATCH,	"Unit input channel count does not match chain")\
  A2_DEFERR(A2_NOOUTPUT,	"Final unit must send to voice output")\
  \
  A2_DEFERR(A2_INTERNAL,	"INTERNAL ERROR")

#define	A2_DEFERR(x, y)	x,
typedef enum A2_errors
{
	A2_OK = 0,
	A2_ALLERRORS
} A2_errors;
#undef	A2_DEFERR


/*---------------------------------------------------------
	Open/close
---------------------------------------------------------*/

typedef enum A2_initflags
{
	/* Flags for A2_config */
	A2_EXPORTALL =	0x00000100,	/* Export all programs! (Debug/tool) */
	A2_TIMESTAMP =	0x00000200,	/* Enable the a2_Now()/a2_Wait() API */
	A2_NOAUTOCNX =	0x00000400,	/* Disable autoconnect (JACK etc) */
	A2_REALTIME =	0x00000800,	/* Configure for realtime operation */
	A2_SILENT =	0x00001000,	/* No API context stderr errors */
	A2_RTSILENT =	0x00002000,	/* No engine context stderr errors */

	A2_INITFLAGS =	0x0000ff00,	/* Mask for the flags above */


	/* NOTE: Application code should NEVER set any of the flags below! */

	A2_SUBSTATE =	0x00010000,	/* State is a substate */

	/* Flags for drivers and configurations */
	A2_ISOPEN =	0x10000000,	/* Object is open/in use */
	A2_STATECLOSE =	0x20000000,	/* Will be closed by engine state */
	A2_CFGCLOSE =	0x40000000,	/* Will be closed by configuration */
} A2_initflags;

#ifdef __cplusplus
};
#endif

#endif /* A2_TYPES_H */
