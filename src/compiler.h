/*
 * compiler.h - Audiality 2 Script (A2S) compiler
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

#ifndef	A2_COMPILER_H
#define	A2_COMPILER_H

#include <setjmp.h>
#include "internals.h"

typedef struct A2_symbol A2_symbol;
typedef struct A2_fixup A2_fixup;
typedef struct A2_coder A2_coder;


/*---------------------------------------------------------
	Error handling
---------------------------------------------------------*/

#define	A2_jumpbuf	jmp_buf
#define	a2c_Try(c)	if(((c)->error = A2_OK), !setjmp((c)->jumpbuf))
#define a2c_Except	else
#ifdef CERRDIE
#	define	a2c_Throw(c, x)	({					\
			printf("THROW %s\n", a2_ErrorString(x));	\
			assert(0);					\
		})
#else
#	define	a2c_Throw(c, x)	longjmp((c)->jumpbuf, ((c)->error = (x)))
#endif


/*---------------------------------------------------------
	Tokens
---------------------------------------------------------*/

typedef enum A2_tokens
{
	/* (lsym points to the matching symbol where applicable) */
	TK_EOF = 256,	/* End of file */
	TK_EOS,		/* End of statement */
	TK_NAMESPACE,	/* sym = namespace symbol */
	TK_VALUE,	/* f = value */
	TK_TEMPREG,	/* Temporary register; i = register index */
	TK_STRING,	/* i = value (handle) */
	TK_BANK,	/* i = value (handle) */
	TK_WAVE,	/* i = value (handle) */
	TK_UNIT,	/* i = value (handle) */
	TK_PROGRAM,	/* i = value (handle) */
	TK_FUNCTION,	/* i = value (entry point index) */
	TK_NAME,	/* sym = new symbol (add or free!) */
	TK_FWDECL,	/* sym = symbol (accumulates fixups!) */
	TK_LABEL,	/* sym->v.i = code position */
	TK_REGISTER,	/* i = register index */
	TK_INSTRUCTION,	/* i = pseudo opcode */
	KW_IMPORT,	/* 'import' directive */
	KW_DEF,		/* 'def' directive */
	KW_STRUCT,	/* 'struct' keyword */
	KW_WIRE,	/* 'wire' keyword */
	KW_TEMPO,	/* 'tempo' macro "instruction" */
	KW_WAVE,	/* 'wave' keyword */
	TK_IF,		/* "if*" conditional variants */
	KW_ELSE,	/* 'else' keyword */
	TK_WHILE,	/* "while|w*" loop variants */
	KW_FOR,		/* "for" loop */
	TK_GE,		/* ">=" */
	TK_LE,		/* "<=" */
	TK_EQ,		/* "==" */
	TK_NE,		/* "!=" */
	KW_AND,		/* 'and' operator keyword */
	KW_OR,		/* 'or' operator keyword */
	KW_XOR,		/* 'xor' operator keyword */
	KW_NOT,		/* 'not' operator keyword */

	/* Attributes for 'wave' definitions etc */
	AT_WAVETYPE,	/* A2_wavetypes */
	TK_WAVETYPE,	/* Token class for 'wavetype' arguments */
	AT_PERIOD,
	AT_SAMPLERATE,	/* (Hz) */
	AT_LENGTH,	/* (sample frames) */
	AT_DURATION,	/* (seconds) */
	AT_FLAG,	/* A2_LOOPED, A2_NORMALIZE etc */
	AT_RANDSEED,	/* RNG seed for the 'rand' instructions */
	AT_NOISESEED	/* RNG seed for the 'noise' wave */
} A2_tokens;

/* Returns "true" if the token represents an immediate value */
static inline int a2_IsValue(A2_tokens tk)
{
	return (tk == TK_VALUE);
}

/* Returns "true" if the token represents an object handle */
static inline int a2_IsHandle(A2_tokens tk)
{
	return (tk == TK_BANK) || (tk == TK_WAVE) || (tk == TK_PROGRAM) ||
			(tk == TK_STRING);
}

/* Returns "true" if the token represents an actual VM register */
static inline int a2_IsRegister(A2_tokens tk)
{
	return (tk == TK_TEMPREG) || (tk == TK_REGISTER);
}

/* Returns "true" if the token uses the v.sym field (may still be NULL!) */
static inline int a2_IsSymbol(A2_tokens tk)
{
	return (tk == TK_NAMESPACE) || (tk == TK_NAME) || (tk == TK_FWDECL) ||
			(tk == TK_LABEL);
}


/*---------------------------------------------------------
	Symbol stack
---------------------------------------------------------*/

typedef enum A2_symflags
{
	A2_SF_EXPORTED =	0x0001,	/* Symbol is to be exported */
	A2_SF_TEMPORARY =	0x0002	/* Temporary symbol created by lexer */
} A2_symflags;

struct A2_symbol
{
	A2_symbol	*next;		/* Next older symbol on stack */
	char		*name;
	A2_symbol	*symbols;	/* Stack of child symbols, if any */
	A2_fixup	*fixups;	/* Fixups for forward branches */
	int		flags;
	A2_tokens	token;		/* Symbol type/token */
	union {
		int		i;
		double		f;
	} v;
};

/* Illegal jump target PC to delay checks for fixups */
#define	A2_UNDEFJUMP	0xff000000

/* Branch fixup entry */
struct A2_fixup
{
	A2_fixup	*next;
	unsigned	pos;		/* Position of instruction to fix */
};


/*---------------------------------------------------------
	Code generator
---------------------------------------------------------*/

typedef char A2_regmap[A2_REGISTERS];

struct A2_coder
{
	A2_coder	*prev;		/* Previously active coder, if any */
	A2_program	*program;	/* Target program, if any */
	unsigned	func;		/* Target function */
	unsigned	*code;		/* Code buffer */
	unsigned	size;		/* Size of buffer (instructions) */
	unsigned	pos;		/* Write position (instructions) */
};


/*---------------------------------------------------------
	Lexer
---------------------------------------------------------*/

/* Number of lexer states to keep */
#define	A2_LEXDEPTH	3

/* Lexer state */
typedef struct A2_lexvalue
{
	int		pos;		/* Source code read position */
	A2_tokens	token;
	union {
		int		i;
		double		f;
		A2_symbol	*sym;
	} v;
} A2_lexvalue;


/*---------------------------------------------------------
	Compiler state
---------------------------------------------------------*/

struct A2_compiler
{
	A2_state	*state;		/* Parent engine state */
	A2_coder	*coder;		/* Current code generator */
	A2_symbol	*symbols;	/* Symbol stack */
	A2_handletab	imports;	/* Imported objects (root namespace) */
	A2_bank		*target;	/* Target bank for exports */
	char		*path;		/* Directory path of current file */
	const char	*source;	/* Source code buffer */
	unsigned	lexbufsize;
	unsigned	lexbufpos;
	char		*lexbuf;	/* Buffer for string parsing */
	A2_lexvalue	l[A2_LEXDEPTH];	/* Lex value buffer; [0] is current */
	A2_regmap	regmap;		/* Current register allocation map */
	int		exportall;	/* Export everything from banks! */
	int		tabsize;	/* Script tab size for messages */
	int		canexport;	/* Current context allows exports! */
	int		inhandler;	/* Disallow timing, RUN, SLEEP ,... */
	int		nocode;		/* Disallow code in current context  */
	int		commawarned;	/* Deprecated comma warning issued */
	A2_jumpbuf	jumpbuf;	/* Buffer for a2c_Try()/a2c_Throw() */
	A2_errors	error;		/* Error from a2c_Throw() */
};


/*---------------------------------------------------------
	Main compiler entry points
---------------------------------------------------------*/

/* Compiler open/close */
A2_compiler *a2_OpenCompiler(A2_state *st, int flags);
void a2_CloseCompiler(A2_compiler *c);

/* Compile Audiality 2 Script source code into VM code. */
A2_errors a2_CompileString(A2_compiler *c, A2_handle bank, const char *code,
		const char *source);
A2_errors a2_CompileFile(A2_compiler *c, A2_handle bank, const char *fn);

#endif	/* A2_COMPILER_H */
