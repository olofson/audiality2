/*
 * compiler.h - Audiality 2 Script (a2s) compiler
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
 */

#ifndef	A2_COMPILER_H
#define	A2_COMPILER_H

#include <setjmp.h>
#include "internals.h"

typedef struct A2_symbol A2_symbol;
typedef struct A2_fixup A2_fixup;
typedef struct A2_coder A2_coder;

/* Compiler error handling */
#define	A2_jumpbuf	jmp_buf
#define	a2c_Try(c)	if(((c)->error = A2_OK), !setjmp((c)->jumpbuf))
#define a2c_Except	else
#ifdef CERRDIE
#	define	a2c_Throw(c, x)	({					\
			printf("THROW %s\n", a2_ErrorString(c->error));	\
			assert(0);					\
		})
#else
#	define	a2c_Throw(c, x)	longjmp((c)->jumpbuf, ((c)->error = (x)))
#endif

typedef enum A2_tokens
{
	/* (lsym points to the matching symbol where applicable) */
	TK_EOF = 256,	/* End of file */
	TK_EOS,		/* End of statement */
	TK_NAMESPACE,	/* lsym = namespace symbol */
	TK_VALUE,	/* lval = value (16:16 fixp) */
#if 0
	TK_STRINGLIT,	/* string = C string */
#endif
	TK_TEMPREG,	/* Temporary register; lval = register index */
	TK_STRING,	/* lval = value (handle) */
	TK_BANK,	/* lval = value (handle) */
	TK_WAVE,	/* lval = value (handle) */
	TK_UNIT,	/* lval = value (handle) */
	TK_PROGRAM,	/* lval = value (handle) */
	TK_FUNCTION,	/* lval = value (entry point index) */
	TK_NAME,	/* lsym = new symbol (add or free!) */
	TK_FWDECL,	/* lsym = symbol (accumulates fixups!) */
	TK_LABEL,	/* lval = value from symbol (code position) */
	TK_REGISTER,	/* lval = value from symbol (register index) */
	TK_INSTRUCTION,	/* lval = value from symbol (pseudo opcode) */
	TK_DEF,		/* 'def' directive */
	TK_STRUCT,	/* 'struct' keyword */
	TK_WIRE,	/* 'wire' keyword */
	TK_TEMPO,	/* 'tempo' macro "instruction" */
	TK_IF,		/* "if*" conditional variants */
	TK_ELSE,	/* 'else' keyword */
	TK_WHILE,	/* "while|w*" loop variants */
	TK_FOR,		/* "for" loop */
	TK_RUN		/* 'run' statement (spawn + wait) */
} A2_tokens;

/*
 * Returns "true" if the token represents a constant value or handle
 *
 * NOTE:
 *	Handles and the like are stored as is! To get them in the right format
 *	for immediate operands, arguments etc, use a2_GetVMValue().
 */
static inline int a2_IsValue(A2_tokens tk)
{
	return (tk == TK_VALUE) || (tk == TK_WAVE) || (tk == TK_PROGRAM) ||
			(tk == TK_STRING);
}

/* Returns "true" if the token represents an actual VM register */
static inline int a2_IsRegister(A2_tokens tk)
{
	return (tk == TK_TEMPREG) || (tk == TK_REGISTER);
}

/* Symbol stack */
struct A2_symbol
{
	A2_symbol	*next;		/* Next older symbol on stack */
	char		*name;
	A2_symbol	*symbols;	/* Stack of child symbols, if any */
	A2_fixup	*fixups;	/* Fixups for forward branches */
	int		exported;
	A2_tokens	token;		/* Symbol type/token */
	int		value;		/* Symbol value or sub-token */
	int		index;		/* Physical object index or similar */
};

/* Illegal jump target PC to delay checks for fixups */
#define	A2_UNDEFJUMP	0xff000000

/* Branch fixup entry */
struct A2_fixup
{
	A2_fixup	*next;
	unsigned	pos;		/* Position of instruction to fix */
};

/* Code generator */
struct A2_coder
{
	A2_coder	*prev;		/* Previously active coder, if any */
	A2_program	*program;	/* Target program, if any */
	unsigned	func;		/* Target function */
	unsigned	*code;		/* Code buffer */
	unsigned	size;		/* Size of buffer (instructions) */
	unsigned	pos;		/* Write position (instructions) */
};

/* Lexer state */
typedef struct A2_lexstate
{
	int		pos;		/* Source code read position */
	A2_tokens	token;
	int		val;		/* Value; int/fixp/handle */
	char		*string;	/* New name, string literal etc */
	A2_symbol	*sym;		/* Value; (new) symbol */
} A2_lexstate;

/* Compiler state */
struct A2_compiler
{
	A2_state	*state;		/* Parent engine state */
	A2_coder	*coder;		/* Current code generator */
	A2_symbol	*symbols;	/* Symbol stack */
	A2_handletab	imports;	/* Imported objects (root namespace) */
	A2_bank		*target;	/* Target bank for exports */
	const char	*source;	/* Source code buffer */
	unsigned	lexbufsize;
	unsigned	lexbufpos;
	char		*lexbuf;	/* Buffer for string parsing */
	A2_lexstate	l, pl;		/* Current and previous lexer states */
	unsigned	regtop;		/* First free register */
	int		exportall;	/* Export everything from banks! */
	int		canexport;	/* Current context allows exports! */
	int		inhandler;	/* Disallow timing, RUN, SLEEP ,... */
	int		nocode;		/* Disallow code in current context  */
	A2_jumpbuf	jumpbuf;	/* Buffer for a2c_Try()/a2c_Throw() */
	A2_errors	error;		/* Error from a2c_Throw() */
};

/* Compiler open/close */
A2_errors a2_OpenCompiler(A2_state *st, int flags);
void a2_CloseCompiler(A2_compiler *c);

/* Compile Audiality 2 Language source code into VM code. */
A2_errors a2_CompileString(A2_compiler *c, A2_handle bank, const char *code,
		const char *source);
A2_errors a2_CompileFile(A2_compiler *c, A2_handle bank, const char *fn);

#endif	/* A2_COMPILER_H */
