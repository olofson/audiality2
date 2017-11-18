/*
 * a2_types.h - Audiality 2 basic data types
 *
 * Copyright 2012-2017 David Olofson <david@olofson.net>
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/*---------------------------------------------------------
	Various widely used Audiality 2 types
---------------------------------------------------------*/

typedef	int A2_handle;
typedef	unsigned A2_timestamp;
typedef struct A2_driver A2_driver;
typedef struct A2_config A2_config;
typedef struct A2_interface A2_interface;

/* Object types (Also used for RCHM handle type tagging!) */
typedef enum A2_otypes
{
	/* Semi-static, API managed objects */
	A2_TBANK = 1,	/* Bank of waves, programs etc */
	A2_TWAVE,	/* Sampled mipmapped wave for oscillators like wtosc */
	A2_TPROGRAM,	/* Instrument/sound/song program (compiled script) */
	A2_TUNIT,	/* Voice structure unit ("class" - not instance!) */
	A2_TCONSTANT,	/* Constant real value */
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

/* Log levels */
typedef enum {
	A2_LOG_INTERNAL =	0x00000001,
	A2_LOG_CRITICAL =	0x00000002,
	A2_LOG_ERROR =		0x00000010,
	A2_LOG_WARNING =	0x00000020,
	A2_LOG_INFO =		0x00000040,
	A2_LOG_DEBUG =		0x00010000
} A2_loglevels;


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
  A2_DEFERR(REFUSE,		"Destruction refused")\
  A2_DEFERR(OOMEMORY,		"Out of memory")\
  A2_DEFERR(OOHANDLES,		"Out of handles")\
  A2_DEFERR(INVALIDHANDLE,	"Invalid handle")\
  A2_DEFERR(FREEHANDLE,		"Handle already returned to the free pool")\
/* ========================================================= */\
  A2_DEFERR(DEADHANDLE,		"Released (not locked) handle used by API")\
  A2_DEFERR(END,		"VM program ended normally")\
  A2_DEFERR(OVERLOAD,		"VM overload; too many instructions "\
  						"back-to-back")\
  A2_DEFERR(ILLEGALOP,		"Illegal VM opcode")\
  A2_DEFERR(LATEMESSAGE,	"API message arrived late to engine context")\
  A2_DEFERR(MANYARGS,		"Too many arguments to VM program")\
  \
  A2_DEFERR(BUFOVERFLOW,	"Buffer overflow")\
  A2_DEFERR(BUFUNDERFLOW,	"Buffer underflow")\
  A2_DEFERR(DIVBYZERO,		"Division by zero")\
  A2_DEFERR(INFLOOP,		"Jump would cause infinite loop")\
  A2_DEFERR(OVERFLOW,		"Value does not fit in numeric type")\
  A2_DEFERR(UNDERFLOW,		"Value too small; would truncate to zero")\
  A2_DEFERR(VALUERANGE,		"Value out of range")\
  A2_DEFERR(INDEXRANGE,		"Index out of range")\
  A2_DEFERR(OUTOFREGS,		"Out of VM registers")\
  A2_DEFERR(LARGEFRAME,		"Function uses too many VM registers")\
  \
  A2_DEFERR(NOTIMPLEMENTED,	"Operation or feature not implemented")\
  A2_DEFERR(OPEN,		"Error opening file")\
  A2_DEFERR(NODRIVER,		"No driver of the required type available")\
  A2_DEFERR(DRIVERNOTFOUND,	"Specified driver not found")\
  A2_DEFERR(DEVICEOPEN,		"Error opening device")\
  A2_DEFERR(ALREADYOPEN,	"Device is already open")\
  A2_DEFERR(ISASSIGNED,		"Object is already assigned to this bank")\
  A2_DEFERR(READ,		"Error reading file or stream")\
  A2_DEFERR(WRITE,		"Error writing file or stream")\
  A2_DEFERR(READONLY,		"Object is read-only")\
  A2_DEFERR(WRITEONLY,		"Object is write-only")\
  A2_DEFERR(STREAMCLOSED,	"Stream closed by the other party")\
  A2_DEFERR(WRONGTYPE,		"Wrong type of data or object")\
  A2_DEFERR(WRONGFORMAT,	"Wrong stream data format")\
  A2_DEFERR(VOICEALLOC,		"Could not allocate voice")\
  A2_DEFERR(VOICEINIT,		"Could not initialize voice")\
  A2_DEFERR(VOICENEST,		"Subvoice nesting depth exceeded")\
  A2_DEFERR(IODONTMATCH,	"Input and output counts don't match")\
  A2_DEFERR(FEWCHANNELS,	"Voice has to few channels for unit")\
  A2_DEFERR(UNITINIT,		"Could not initialize unit instance")\
  A2_DEFERR(NOTFOUND,		"Object not found")\
  A2_DEFERR(NOOBJECT,		"Handle is not attached to an object")\
  A2_DEFERR(NOXINSERT,		"No 'xinsert' unit found in voice structure")\
  A2_DEFERR(NOSTREAMCLIENT,	"'xinsert' client not set up for streaming")\
  A2_DEFERR(NOREPLACE,		"Unit does not implement replacing output"\
  						" mode")\
  A2_DEFERR(NOTOUTPUT,		"Tried to wire inputs to voice output bus")\
  A2_DEFERR(NOUNITS,		"Voice has no units")\
  A2_DEFERR(MULTIINLINE,	"Voice cannot have multiple inline units")\
  A2_DEFERR(CHAINMISMATCH,	"Unit input count does not match chain")\
  A2_DEFERR(NOOUTPUT,		"Final unit must send to voice output")\
  A2_DEFERR(BLINDCHAIN,		"Outputs wired to nothing, as there are no"\
  						" inputs downstream")\
  A2_DEFERR(EXPORTDECL,		"Export already declared")\
  A2_DEFERR(SYMBOLDEF,		"Symbol already defined")\
  A2_DEFERR(UNDEFSYM,		"Undefined symbols in program")\
  A2_DEFERR(MESSAGEDEF,		"Handler for this message already defined")\
  A2_DEFERR(ONLYLOCAL,		"Symbols can only be local in this scope")\
  A2_DEFERR(DECLNOINIT,		"Declared variable not initialized")\
  A2_DEFERR(COUTWIRED,		"Control output is already wired")\
  \
  A2_DEFERR(EXPEOS,		"Expected end of statement")\
  A2_DEFERR(EXPSTATEMENT,	"Expected a non-empty statement")\
  A2_DEFERR(EXPCLOSE,		"Expected closing brace")\
  A2_DEFERR(EXPNAME,		"Expected name")\
  A2_DEFERR(EXPVALUE,		"Expected value")\
  A2_DEFERR(EXPVALUEHANDLE,	"Expected value or handle")\
  A2_DEFERR(EXPINTEGER,		"Expected integer value")\
  A2_DEFERR(EXPSTRING,		"Expected string literal")\
  A2_DEFERR(EXPSTRINGORNAME,	"Expected string literal or name")\
  A2_DEFERR(EXPVARIABLE,	"Expected variable")\
  A2_DEFERR(EXPCTRLREGISTER,	"Expected control register")\
  A2_DEFERR(EXPLABEL,		"Expected label")\
  A2_DEFERR(EXPPROGRAM,		"Expected program")\
  A2_DEFERR(EXPFUNCTION,	"Expected function declaration")\
  A2_DEFERR(EXPUNIT,		"Expected unit")\
  A2_DEFERR(EXPBODY,		"Expected body")\
  A2_DEFERR(EXPOP,		"Expected operator")\
  A2_DEFERR(EXPBINOP,		"Expected binary operator")\
  A2_DEFERR(EXPCONSTANT,	"Expected constant")\
  A2_DEFERR(EXPWAVETYPE,	"Expected wave type identifier")\
  A2_DEFERR(EXPEXPRESSION,	"Expected expression")\
  A2_DEFERR(EXPVOICEEOS,	"Expected voice index or end of statement")\
  \
  A2_DEFERR(NEXPEOF,		"Unexpected end of file")\
  A2_DEFERR(NEXPNAME,		"Undefined symbol")\
  A2_DEFERR(NEXPVALUE,		"Value not expected here")\
  A2_DEFERR(NEXPHANDLE,		"Handle not expected here")\
  A2_DEFERR(NEXPTOKEN,		"Unexpected token")\
  A2_DEFERR(NEXPELSE,		"'else' not applicable here")\
  A2_DEFERR(NEXPLABEL,		"Label not expected here")\
  A2_DEFERR(NEXPMODIFIER,	"Value modifier not expected here")\
  A2_DEFERR(NEXPDECPOINT,	"Decimal point not expected here")\
  \
  A2_DEFERR(BADFORMAT,		"Bad file or device I/O format")\
  A2_DEFERR(BADSAMPLERATE,	"Unsupported audio sample rate")\
  A2_DEFERR(BADBUFSIZE,		"Unsupported audio buffer size")\
  A2_DEFERR(BADCHANNELS,	"Unsupported audio channel count")\
  A2_DEFERR(BADTYPE,		"Invalid type ID")\
  A2_DEFERR(BADBANK,		"Invalid bank handle")\
  A2_DEFERR(BADWAVE,		"Invalid waveform handle")\
  A2_DEFERR(BADPROGRAM,		"Invalid program handle")\
  A2_DEFERR(BADENTRY,		"Invalid program entry point")\
  A2_DEFERR(BADVOICE,		"Voice does not exist, or bad voice id")\
  A2_DEFERR(BADLABEL,		"Bad label name")\
  A2_DEFERR(BADVALUE,		"Bad value")\
  A2_DEFERR(BADJUMP,		"Illegal jump target position")\
  A2_DEFERR(BADOPCODE,		"Invalid VM opcode")\
  A2_DEFERR(BADREGISTER,	"Invalid VM register index")\
  A2_DEFERR(BADREG2,		"Invalid VM register index, second argument")\
  A2_DEFERR(BADIMMARG,		"Immediate argument out of range")\
  A2_DEFERR(BADVARDECL,		"Variable cannot be declared here")\
  A2_DEFERR(BADOCTESCAPE,	"Bad octal escape format in string literal")\
  A2_DEFERR(BADDECESCAPE,	"Bad decimal escape format in string literal")\
  A2_DEFERR(BADHEXESCAPE,	"Bad hex escape format in string literal")\
  A2_DEFERR(BADIFNEST,		"Nested 'if' without braces")\
  A2_DEFERR(BADELSE,		"Use of 'else' after non-braced statement")\
  A2_DEFERR(BADLIBVERSION,	"Linked A2 lib incompatible with application")\
  A2_DEFERR(BADDELIMITER,	"Unexpected ',' delimiter (old script?)")\
  \
  A2_DEFERR(CANTEXPORT,		"Cannot export from this scope")\
  A2_DEFERR(CANTINPUT,		"Unit cannot have inputs")\
  A2_DEFERR(CANTOUTPUT,		"Unit cannot have outputs")\
  A2_DEFERR(NOPROGHERE,		"Program cannot be declared here")\
  A2_DEFERR(NOMSGHERE,		"Message cannot be declared here")\
  A2_DEFERR(NOFUNCHERE,		"Function cannot be declared here")\
  A2_DEFERR(NOTUNARY,		"Not a unary operator")\
  A2_DEFERR(NOCODE,		"Code not allowed here")\
  A2_DEFERR(NOTIMING,		"Timing instructions not allowed here")\
  A2_DEFERR(NORUN,		"Cannot run program from here")\
  A2_DEFERR(NORETURN,		"'return' not allowed in this context")\
  A2_DEFERR(NOEXPORT,		"Cannot export this kind of symbol")\
  A2_DEFERR(NOWAKEFORCE,	"'wake' and 'force' not applicable here")\
  A2_DEFERR(NOPORT,		"Port is unavailable or does not exist")\
  A2_DEFERR(NOINPUT,		"Unit with inputs where there is no audio")\
  A2_DEFERR(NONAME,		"Object has no name")\
  \
  A2_DEFERR(INTERNAL,		"INTERNAL ERROR")	/* Must be last! */

#define	A2_DEFERR(x, y)	A2_##x,
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
	A2_TIMESTAMP =	0x00000200,	/* Enable the a2_Timestamp*() API */
	A2_NOAUTOCNX =	0x00000400,	/* Disable autoconnect (JACK etc) */
	A2_REALTIME =	0x00000800,	/* Configure for realtime operation */
	A2_SILENT =	0x00001000,	/* No API context stderr errors */
	A2_RTSILENT =	0x00002000,	/* No engine context stderr errors */
	A2_NOSHARED =	0x00004000,	/* No bank sharing (also a2_Load().)*/

	A2_INITFLAGS =	0x000fff00,	/* Mask for the flags above */


	/* NOTE: Application code should NEVER set any of the flags below! */

	A2_SUBSTATE =	0x00100000,	/* State is a substate */

	/* Flags for drivers and configurations */
	A2_ISOPEN =	0x10000000,	/* Object is open/in use */
	A2_AUTOCLOSE =	0x20000000,	/* Will be closed by parent object */
	A2_NOREF =	0x40000000	/* Don't count as parent reference */
} A2_initflags;

#ifdef __cplusplus
};
#endif

#endif /* A2_TYPES_H */
