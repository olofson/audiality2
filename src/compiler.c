/*
 * compiler.c - Audiality 2 Script (A2S) compiler
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
 *
===============================================================================
WARNING: The parser uses setjmp()/longjmp() for error handling.
WARNING: Calls with the a2c_ prefix MUST ONLY be used with a2_Try()!
===============================================================================
 */

/* Define to dump compiled banks to "out.a2b" */
#undef	A2_OUT_A2B

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "compiler.h"
#include "units/inline.h"


/*---------------------------------------------------------
	Debug tools
---------------------------------------------------------*/

/* Calculate current position in source code. */
static void a2c_CalculatePos(A2_compiler *c, int pos, int *line, int *col)
{
	int i;
	*line = 1;
	*col = 1;
	for(i = 0; i < pos ; ++i)
		switch(c->source[i])
		{
		  case '\n':
			++*line;
			*col = 1;
			break;
		  case '\t':
			*col += c->tabsize + 1;
			*col -= *col % c->tabsize;
			break;
		  default:
			*col += 1;
			break;
		}
}


/* Print source code line containing position 'pos' to 'f'. */
static void a2c_DumpLine(A2_compiler *c, int pos, int mark, FILE *f)
{
	int line, col;
	int cnt;
	a2c_CalculatePos(c, pos, &line, &col);
	while(pos && (c->source[pos - 1] != '\n'))
		--pos;
	cnt = pos;
	while(c->source[cnt] && (c->source[cnt] != '\n'))
		++cnt;
	cnt -= pos;
	/* FIXME: Convert tabs to spaces, to honor A2_PTABSIZE! */
	fprintf(f, "%6.1d: ", line);
	fwrite(c->source + pos, cnt, 1, f);
	fputs("\n", f);
	if(mark)
	{
		col += 8;
		while(col--)
		putc(' ', f);
		fputs("^\n", f);
	}
}


#define	A2_DEFERR(x, y)	y,
static const char *a2_errnames[] = {
	"Ok - no error!",
	A2_ALLERRORS
};
#undef	A2_DEFERR

static char a2_errbuf[128];

const char *a2_ErrorString(unsigned errorcode)
{
	if(errorcode < A2_INTERNAL)
		return a2_errnames[errorcode];
	else
	{
		a2_errbuf[sizeof(a2_errbuf) - 1] = 0;
		snprintf(a2_errbuf, sizeof(a2_errbuf) - 1,
				"INTERNAL ERROR #%d; please report to "
				"<david@olofson.net>", errorcode - A2_INTERNAL);
		return a2_errbuf;
	}
}

#define	A2_DI(x)	#x,
static const char *a2_insnames[A2_OPCODES] = {
	/* Program flow control */
	"END",
	A2_ALLINSTRUCTIONS
};
#undef	A2_DI

static const char *a2_regnames[A2_CREGISTERS] = {
	"TICK",	"TR"
};

static void a2_PrintRegName(unsigned r)
{
	if(r < A2_CREGISTERS)
		fputs(a2_regnames[r], stderr);
	else
		fprintf(stderr, "R%d", r);
}

void a2_DumpIns(unsigned *code, unsigned pc)
{
	A2_instruction *ins = (A2_instruction *)(code + pc);
	fprintf(stderr, "%d:\t%-8.8s", pc, a2_insnames[ins->opcode]);
	switch(ins->opcode)
	{
	  /* No arguments */
	  case OP_END:
	  case OP_RETURN:
	  case OP_SLEEP:
	  case OP_KILLA:
	  case OP_INITV:
	  case OP_SETALL:
	  case A2_OPCODES:	/* (Warning eliminator) */
		break;
	  /* <integer(a2)> */
	  case OP_JUMP:
	  case OP_WAKE:
	  case OP_FORCE:
	  case OP_SENDA:
	  case OP_SENDS:
	  case OP_CALL:
	  case OP_SPAWND:
	  case OP_SIZEOF:
		fprintf(stderr, "%d", ins->a2);
		break;
	  /* <16:16(a3)> */
	  case OP_DELAY:
	  case OP_TDELAY:
	  case OP_PUSH:
	  case OP_DEBUG:
		fprintf(stderr, "%f", ins->a3 / 65536.0f);
		break;
	  /* <register(a1)> */
	  case OP_DELAYR:
	  case OP_TDELAYR:
	  case OP_PUSHR:
	  case OP_SET:
	  case OP_DEBUGR:
	  case OP_SIZEOFR:
		a2_PrintRegName(ins->a1);
		break;
	  /* <register(a2)> */
	  case OP_SPAWNDR:
		a2_PrintRegName(ins->a2);
		break;
	  /* <register(a1), 16:16(a3)> */
	  case OP_LOAD:
	  case OP_ADD:
	  case OP_MUL:
	  case OP_MOD:
	  case OP_QUANT:
	  case OP_RAND:
		a2_PrintRegName(ins->a1);
		fprintf(stderr, " %f", ins->a3 / 65536.0f);
		break;
	  /* <register(a1), integer(a2)> */
	  case OP_LOOP:
	  case OP_JZ:
	  case OP_JNZ:
	  case OP_JG:
	  case OP_JL:
	  case OP_JGE:
	  case OP_JLE:
	  case OP_SPAWNV:
		a2_PrintRegName(ins->a1);
		fprintf(stderr, " %d", ins->a2);
		break;
	  /* <index(a1)> */
	  case OP_KILL:
	  case OP_WAIT:
		fprintf(stderr, "%d", ins->a1);
		break;
	  /* <index(a1), integer(a2)> */
	  case OP_SPAWN:
	  case OP_SEND:
		fprintf(stderr, "%d %d", ins->a1, ins->a2);
		break;
	  /* <register(a1), register(a2)> */
	  case OP_LOADR:
	  case OP_ADDR:
	  case OP_SUBR:
	  case OP_MULR:
	  case OP_DIVR:
	  case OP_MODR:
	  case OP_QUANTR:
	  case OP_RANDR:
	  case OP_SPAWNR:
	  case OP_SPAWNVR:
	  case OP_SENDR:
	  case OP_P2DR:
	  case OP_NEGR:
	  case OP_GR:
	  case OP_LR:
	  case OP_GER:
	  case OP_LER:
	  case OP_EQR:
	  case OP_NER:
	  case OP_ANDR:
	  case OP_ORR:
	  case OP_XORR:
	  case OP_NOTR:
		a2_PrintRegName(ins->a1);
		fprintf(stderr, " ");
		a2_PrintRegName(ins->a2);
		break;
	}
	fprintf(stderr, "\n");
}


#if defined(DUMPTOKENS) || (DUMPLSTRINGS(1)+0)
static const char *a2c_T2S(A2_tokens tk)
{
	switch(tk)
	{
	  case TK_EOF:		return "TK_EOF";
	  case TK_EOS:		return "TK_EOS";
	  case TK_NAMESPACE:	return "TK_NAMESPACE";
	  case TK_VALUE:	return "TK_VALUE";
	  case TK_TEMPREG:	return "TK_TEMPREG";
	  case TK_STRING:	return "TK_STRING";
	  case TK_BANK:		return "TK_BANK";
	  case TK_WAVE:		return "TK_WAVE";
	  case TK_UNIT:		return "TK_UNIT";
	  case TK_PROGRAM:	return "TK_PROGRAM";
	  case TK_FUNCTION:	return "TK_FUNCTION";
	  case TK_NAME:		return "TK_NAME";
	  case TK_FWDECL:	return "TK_FWDECL";
	  case TK_LABEL:	return "TK_LABEL";
	  case TK_REGISTER:	return "TK_REGISTER";
	  case TK_INSTRUCTION:	return "TK_INSTRUCTION";
	  case KW_IMPORT:	return "KW_IMPORT";
	  case KW_DEF:		return "KW_DEF";
	  case KW_STRUCT:	return "KW_STRUCT";
	  case KW_WIRE:		return "KW_WIRE";
	  case KW_TEMPO:	return "KW_TEMPO";
	  case KW_WAVE:		return "KW_WAVE";
	  case TK_IF:		return "TK_IF";
	  case KW_ELSE:		return "KW_ELSE";
	  case TK_WHILE:	return "TK_WHILE";
	  case KW_FOR:		return "KW_FOR";
	  case TK_GE:		return "TK_GE";
	  case TK_LE:		return "TK_LE";
	  case TK_EQ:		return "TK_EQ";
	  case TK_NE:		return "TK_NE";
	  case KW_AND:		return "KW_AND";
	  case KW_OR:		return "KW_OR";
	  case KW_XOR:		return "KW_XOR";
	  case KW_NOT:		return "KW_NOT";

	  case AT_WAVETYPE:	return "AT_WAVETYPE";
	  case TK_WAVETYPE:	return "TK_WAVETYPE";
	  case AT_PERIOD:	return "AT_PERIOD";
	  case AT_SAMPLERATE:	return "AT_SAMPLERATE";
	  case AT_LENGTH:	return "AT_LENGTH";
	  case AT_DURATION:	return "AT_DURATION";
	  case AT_FLAG:		return "AT_FLAG";
	  case AT_RANDSEED:	return "AT_RANDSEED";
	  case AT_NOISESEED:	return "AT_NOISESEED";
	}
	return "<unknown>";
}
#endif


/*---------------------------------------------------------
	Symbols
---------------------------------------------------------*/

static A2_symbol *a2_NewSymbol(const char *name, A2_tokens token)
{
	A2_symbol *s = (A2_symbol *)calloc(1, sizeof(A2_symbol));
	if(!s)
		return NULL;
	s->name = strdup(name);
	s->token = token;
	return s;
}

static void a2_FreeSymbol(A2_symbol *s)
{
	free(s->name);
	while(s->symbols)
	{
		A2_symbol *cs = s->symbols;
		s->symbols = cs->next;
		a2_FreeSymbol(cs);
	}
	free(s);
}

static void a2_PushSymbol(A2_symbol **stack, A2_symbol *s)
{
#ifdef DEBUG	
	if(s->next)
	{
		fprintf(stderr, "INTERNAL ERROR: Tried to push symbol '%s', "
				"which has a non-NULL 'next' field!\n",
				s->name);
		*(volatile char *)NULL = 0;
	}
#endif
	SYMBOLDBG(
		if(a2_IsValue(s->token))
			fprintf(stderr, "a2_PushSymbol(\"%s\" %s (%d) v:%f)\n",
				s->name, a2c_T2S(s->token), s->token, s->v.f);
		else
			fprintf(stderr, "a2_PushSymbol(\"%s\" %s (%d) v:%d)\n",
				s->name, a2c_T2S(s->token), s->token, s->v.i);
	)
	s->next = *stack;
	*stack = s;
}

static A2_symbol *a2_FindSymbol(A2_state *st, A2_symbol *s, const char *name)
{
	SYMBOLDBG(fprintf(stderr, "a2_FindSymbol('%s'): ", name);)
	for( ; s; s = s->next)
		if(!strcmp(name, s->name))
		{
			SYMBOLDBG(fprintf(stderr, "FOUND!\n");)
			return s;
		}
	SYMBOLDBG(fprintf(stderr, "NOT FOUND!\n");)
	return NULL;
}


/*---------------------------------------------------------
	Handles and objects
---------------------------------------------------------*/

/* Add dependency on 'h' to current bank, if there isn't one already */
static void a2c_AddDependency(A2_compiler *c, A2_handle h)
{
	int ind;
	if(a2ht_FindItem(&c->target->deps, h) >= 0)
		return;		/* Already in there! */
	if((ind = a2ht_AddItem(&c->target->deps, h)) < 0)
		a2c_Throw(c, -ind);
}


/*---------------------------------------------------------
	Coder
---------------------------------------------------------*/

/*
 * Convert a double precision value into 16:16 fixed point for the VM. May throw
 * the following exceptions:
 *	A2_OVERFLOW if the parsed value is too large to fit in a 16:16 fixp
 *	A2_UNDERFLOW if a non-zero parsed value is truncated to zero
 */
static int a2c_Num2VM(A2_compiler *c, double v)
{
	int fxv = floor(v * 65536.0f + 0.5f);
	if((v > 32767.0f) && (v < -32768.0f))
		a2c_Throw(c, A2_OVERFLOW);
	if(v && !fxv)
		a2c_Throw(c, A2_UNDERFLOW);
	return fxv;
}


/*
 * Convert a double precision value into int, verifying that the value actually
 * is integer and within range.
 */
static int a2c_Num2Int(A2_compiler *c, double v)
{
	int fxv = (int)v;
	if((v > 2147483647.0f) && (v < -2147483648.0f))
		a2c_Throw(c, A2_OVERFLOW);
	if(v != fxv)
		a2c_Throw(c, A2_EXPINTEGER);
	return fxv;
}


/* Create a new coder for program 'p', function index 'func' */
static void a2c_PushCoder(A2_compiler *c, A2_program *p, unsigned func)
{
	A2_coder *cdr = (A2_coder *)calloc(1, sizeof(A2_coder));
	if(!cdr)
		a2c_Throw(c, A2_OOMEMORY);
	if(p)
		cdr->program = p;
	else if(c->coder)
		cdr->program = c->coder->program;
	cdr->func = func;
	cdr->prev = c->coder;
	c->coder = cdr;
}


/* Pop current coder, transfering the code to the program it was assigned to */
static void a2c_PopCoder(A2_compiler *c)
{
	A2_instruction *ins;
	A2_function *fn;
	A2_coder *cdr = c->coder;
	if(!cdr)
		a2c_Throw(c, A2_INTERNAL + 130);	/* No coder!? */
	fn = cdr->program->funcs + cdr->func;
	fn->code = (unsigned *)realloc(cdr->code,
			(cdr->pos + 1) * sizeof(unsigned));
	if(!fn->code)
		a2c_Throw(c, A2_OOMEMORY);
	ins = (A2_instruction *)(fn->code + cdr->pos);
	ins->opcode = OP_END;
	c->coder = cdr->prev;
	free(cdr);
}


/*
 * Issue VM instruction 'op' with arguments 'reg' and 'arg'.
 * Argument range checking is done, and 'arg' is treated as integer or 16:16
 * and handled apropriately, based on the opcode.
 */
static void a2c_Code(A2_compiler *c, unsigned op, unsigned reg, int arg)
{
	A2_instruction *ins;
	A2_coder *cdr = c->coder;
	int longins = 0;
	if(c->nocode)
		a2c_Throw(c, A2_NOCODE);
	if(cdr->pos + 3 >= cdr->size)
	{
		int i, ns = cdr->size;
		unsigned *nc;
		if(ns)
			while(cdr->pos + 3 >= ns)
				ns = ns * 3 / 2;
		else
			ns = 64;
		nc = (unsigned *)realloc(cdr->code, ns * sizeof(unsigned));
		if(!nc)
			a2c_Throw(c, A2_OOMEMORY);
		cdr->code = nc;
		cdr->size = ns;
		for(i = cdr->pos; i < cdr->size; ++i)
		{
			cdr->code[i] = 0;
			((A2_instruction *)(cdr->code + i))->opcode = OP_END;
		}
	}
	ins = (A2_instruction *)(cdr->code + cdr->pos);
	if(op >= A2_OPCODES)
		a2c_Throw(c, A2_BADOPCODE);
	if(reg >= A2_REGISTERS)
		a2c_Throw(c, A2_BADREGISTER);
	switch(op)
	{
	  case OP_END:
		if(c->inhandler)
			a2c_Throw(c, A2_INTERNAL + 103);
		break;
	  case OP_RETURN:
		if(!c->coder->func)	/* Not allowed in main program! */
			a2c_Throw(c, A2_NORETURN);
		break;
	  case OP_WAKE:
	  case OP_FORCE:
/*TODO: Check these? (Program main entry point - not current function!) */
		break;
	  case OP_JUMP:
	  case OP_LOOP:
	  case OP_JZ:
	  case OP_JNZ:
	  case OP_JG:
	  case OP_JL:
	  case OP_JGE:
	  case OP_JLE:
		if(arg == A2_UNDEFJUMP)
			arg = 0;
		else
		{
			if(arg < 0)
				a2c_Throw(c, A2_BADJUMP);
			if(arg == cdr->pos)
				a2c_Throw(c, A2_INFLOOP);
			if(arg > cdr->pos)
				a2c_Throw(c, A2_BADJUMP);
		}
		break;
	  case OP_SPAWN:
	  case OP_SPAWND:
	  case OP_SPAWNV:
		if(!a2_GetProgram(c->state, arg))
			a2c_Throw(c, A2_BADPROGRAM);
		break;
	  case OP_SEND:
	  case OP_SENDR:
	  case OP_SENDA:
	  case OP_SENDS:
	  case OP_CALL:
		if(!arg)	/* This is the main program...! */
			a2c_Throw(c, A2_BADENTRY);
		if(arg > A2_MAXEPS)
			a2c_Throw(c, A2_BADENTRY);
		break;
	  case OP_LOADR:
		if(arg == reg)
			return;	/* NOP! */
	  case OP_ADDR:
	  case OP_SUBR:
	  case OP_MULR:
	  case OP_DIVR:
	  case OP_MODR:
	  case OP_RANDR:
	  case OP_P2DR:
	  case OP_NEGR:
	  case OP_GR:
	  case OP_LR:
	  case OP_GER:
	  case OP_LER:
	  case OP_EQR:
	  case OP_NER:
	  case OP_ANDR:
	  case OP_ORR:
	  case OP_XORR:
	  case OP_NOTR:
	  case OP_QUANTR:
	  case OP_SPAWNR:
	  case OP_SPAWNDR:
	  case OP_SPAWNVR:
#if 0
	  case OP_RAMPR:
	  case OP_DETACHR:
#endif
		if((arg < 0) || (arg > A2_REGISTERS))
			a2c_Throw(c, A2_BADREG2);
		break;
	  case OP_WAIT:
/*FIXME: Should probably add all instructions that don't use 'arg' here... */
		break;
	  case OP_DELAY:
	  case OP_TDELAY:
	  case OP_LOAD:
	  case OP_ADD:
	  case OP_MUL:
	  case OP_MOD:
	  case OP_QUANT:
	  case OP_RAND:
#if 0
	  case OP_RAMP:
#endif
	  case OP_PUSH:
	  case OP_DEBUG:
		longins = 1;
		break;
	  default:
		break;
	}
	ins->opcode = op;
	ins->a1 = reg;
	if(longins)
	{
		ins->a2 = 0;
		ins->a3 = arg;
	}
	else
	{
		if((arg < 0) || (arg > 0xffff))
			a2c_Throw(c, A2_BADIMMARG);
		ins->a2 = arg;
	}
	DUMPCODE(a2_DumpIns(cdr->code, cdr->pos);)
	cdr->pos += longins ? 2 : 1;
}


static void a2c_Codef(A2_compiler *c, unsigned op, unsigned reg, double arg)
{
	a2c_Code(c, op, reg, a2c_Num2VM(c, arg));
}


static inline void a2c_SetA2(A2_compiler *c, int pos, int val)
{
	if((val < 0) || (val > 0xffff))
		a2c_Throw(c, A2_BADIMMARG);
#ifdef DEBUG
	if((pos < 0) || (pos >= c->coder->size))
		a2c_Throw(c, A2_INTERNAL + 104);	/* Bad code position! */
#endif
	((A2_instruction *)(c->coder->code + pos))->a2 = val;
}


static void a2c_DoFixups(A2_compiler *c, A2_symbol *s)
{
	while(s->fixups)
	{
		A2_fixup *fx = s->fixups;
		s->fixups = fx->next;
		a2c_SetA2(c, fx->pos, s->v.i);
		DUMPCODE(
			fprintf(stderr, "FIXUP: ");
			a2_DumpIns(c->coder->code, fx->pos);
		)
		free(fx);
	}
}


/*---------------------------------------------------------
	Lexer
---------------------------------------------------------*/

static inline A2_handle a2_find_import(A2_compiler *c, const char *name)
{
	A2_handle h;
	int i;
	for(i = 0; i < c->imports.nitems; ++i)
		if((h = a2_Get(c->state, c->imports.items[i], name)) >= 0)
			return h;
	return -1;
}

/*
 * Grab 'n' characters from s, and append a 'nul' byte.
 * (NOT equivalent to strndup())
 */
static inline char *a2c_ndup(const char *s, size_t n)
{
	char *new = malloc(n + 1);
	if(!new)
		return NULL;
	new[n] = '\0';
	return memcpy(new, s, n);
}

static inline int a2_GetChar(A2_compiler *c)
{
	int ch = c->source[c->l[0].pos];
	DUMPSOURCE(static int last_printed_pos = -1;)
	if(!ch)
		return -1;
	DUMPSOURCE(
		if((ch == '\n') && (c->l[0].pos != last_printed_pos))
		{
			fputs("\n", stderr);
			a2c_DumpLine(c, c->l[0].pos + 1, 0, stderr);
			last_printed_pos = c->l[0].pos;
		}
	)
	++c->l[0].pos;
	return ch;
}

static inline void a2_UngetChar(A2_compiler *c)
{
#ifdef DEBUG
	if(!c->l[0].pos)
		a2c_Throw(c, A2_INTERNAL + 140);
#endif
	--c->l[0].pos;
}

static void a2_LexBufAdd(A2_compiler *c, int ch)
{
	if(c->lexbufpos == c->lexbufsize)
	{
		char *nlb;
		c->lexbufsize = (c->lexbufsize + 1) * 3 / 2;
		if(!(nlb = (char *)realloc(c->lexbuf, c->lexbufsize)))
			a2c_Throw(c, A2_OOMEMORY);
		c->lexbuf = nlb;
	}
	c->lexbuf[c->lexbufpos++] = ch;
}


/*
 * Attempt to parse a decimal value and return it as double precision floating
 * point through *v. Returns A2_OK if the operation is successful.
 *
 * If 'required' is true, an exception will be thrown if a valid number cannot
 * be parsed. If 'required' is false, failure to parse a valid number is
 * indicated through the return code.
 */
static A2_errors a2_GetNum(A2_compiler *c, int ch, double *v, int required)
{
	int startpos = c->l[0].pos;
	int sign = 1;
	double val = 0.0f;
	unsigned xp = 0;
	int valid = 0;	/* At least one figure, or this is not a number! */
	if(ch == '-')
	{
		sign = -1;
		ch = a2_GetChar(c);
	}
	while(1)
	{
		if((ch >= '0') && (ch <= '9'))
		{
			xp *= 10;
			val *= 10.0f;
			val += ch - '0';
			valid = 1;
		}
		else if(ch == '.')
		{
			if(xp)
				break;
			xp = 1;
		}
		else
		{
			if(!valid)
				break;
			val *= sign;
			if(xp)
				val /= xp;
			if(ch == 'n')
				val /= 12.0f;
			else if(ch == 'f')
				val = a2_F2P(val);
			else
				a2_UngetChar(c);
			*v = val;
			return A2_OK;
		}
		ch = a2_GetChar(c);
	}
	c->l[0].pos = startpos;
	if(required)
		a2c_Throw(c, A2_BADVALUE);
	else
		return A2_BADVALUE;
}


/*
 * Simple base-n integer number parser for string escapes.
 *
 * If 'figures' is positive, it specifies the number of figures to read, and
 * the call will fail if that number of valid figures cannot be read.
 *
 * If 'figures' is negative, this function reads at most abs(figures) figures,
 * and fails only if it gets no figures at all.
 */
static inline int get_figure(A2_compiler *c, int base)
{
	int n = a2_GetChar(c);
	if(n >= '0' && n <= '9')
		n -= '0';
	else if(n >= 'a' && n <= 'z')
		n -= 'a' - 10;
	else if(n >= 'A' && n <= 'Z')
		n -= 'A' - 10;
	else
		return -1;
	if(n >= base)
		return -1;
	return n;
}

static int a2_GetIntNum(A2_compiler *c, int base, int figures)
{
	int value = 0;
	int limitonly = 0;
	int figures_read = 0;
	if(figures < 0)
	{
		figures = -figures;
		limitonly = 1;
	}
	while(figures--)
	{
		int n = get_figure(c, base);
		if(n < 0)
		{
			if(limitonly && figures_read)
				return value;
			else
				return n;
		}
		value *= base;
		value += n;
		++figures_read;
	}
	return value;
}


/* Parse a double quoted string with some basic C style control codes */
static int a2c_LexString(A2_compiler *c)
{
	char *s;
	while(1)
	{
		int ch = a2_GetChar(c);
		switch(ch)
		{
		  case -1:
			a2c_Throw(c, A2_NEXPEOF);
		  case '\\':
			ch = a2_GetChar(c);
			switch(ch)
			{
			  case -1:
				a2c_Throw(c, A2_NEXPEOF);
			  case '0':
			  case '1':
			  case '2':
			  case '3':
				a2_UngetChar(c);
			  	ch = a2_GetIntNum(c, 8, -3);
				if(ch < 0)
					a2c_Throw(c, A2_BADOCTESCAPE);
				break;
			  case 'a':
				ch = '\a';
				break;
			  case 'b':
				ch = '\b';
				break;
			  case 'd':
				ch = a2_GetIntNum(c, 10, -3);
				if(ch < 0)
					a2c_Throw(c, A2_BADDECESCAPE);
				break;
			  case 'f':
				ch = '\f';
				break;
			  case 'n':
				ch = '\n';
				break;
			  case 'r':
				ch = '\r';
				break;
			  case 't':
				ch = '\t';
				break;
			  case 'v':
				ch = '\v';
				break;
			  case 'x':
				ch = a2_GetIntNum(c, 16, -2);
				if(ch < 0)
					a2c_Throw(c, A2_BADHEXESCAPE);
				break;
			  default:
				a2_LexBufAdd(c, ch);
				continue;
			}
			break;
		  case '\n':
		  case '\r':
		  case '\t':
			continue;
		  default:
			break;
		}
		if(ch == '"')
			break;
		a2_LexBufAdd(c, ch);
	}
	if(!(s = a2c_ndup(c->lexbuf, c->lexbufpos)))
		a2c_Throw(c, A2_OOMEMORY);
	c->l[0].token = TK_STRING;
	c->l[0].v.i = a2_NewString(c->state, s);
	free(s);
	if(c->l[0].v.i < 0)
		a2c_Throw(c, -c->l[0].v.i);
	a2c_AddDependency(c, c->l[0].v.i);
	return c->l[0].token;
}


static int a2c_GetOpOrChar(A2_compiler *c, int ch)
{
	if(a2_GetChar(c) == '=')
		switch(ch)
		{
		  case '>':	return (c->l[0].token = TK_GE);
		  case '<':	return (c->l[0].token = TK_LE);
		  case '=':	return (c->l[0].token = TK_EQ);
		  case '!':	return (c->l[0].token = TK_NE);
		}
	a2_UngetChar(c);
	return (c->l[0].token = ch);
}


/* Release any non-grabbed symbols created by the lexer. */
static void a2c_FreeToken(A2_compiler *c, A2_lexvalue *l)
{
	if(!a2_IsSymbol(l->token))
		return;
	if(!(l->v.sym->flags & A2_SF_TEMPORARY))
		return;
	a2_FreeSymbol(l->v.sym);
}


static double a2c_GetValue(A2_compiler *c, A2_lexvalue *l)
{
	switch(l->token)
	{
	  case TK_VALUE:
		return l->v.f;
	  default:
		a2c_Throw(c, A2_INTERNAL + 142);
	}
}


static unsigned a2c_GetHandle(A2_compiler *c, A2_lexvalue *l)
{
	switch(l->token)
	{
	  case TK_STRING:
	  case TK_BANK:
	  case TK_WAVE:
	  case TK_UNIT:
	  case TK_PROGRAM:
		return l->v.i;
	  default:
		a2c_Throw(c, A2_INTERNAL + 143);
	}
}


static unsigned a2c_GetIndex(A2_compiler *c, A2_lexvalue *l)
{
	switch(l->token)
	{
	  case TK_TEMPREG:
	  case TK_REGISTER:
	  case TK_FUNCTION:
	  case TK_INSTRUCTION:
		return l->v.i;
	  case TK_LABEL:
		return l->v.sym->v.i;
	  default:
		a2c_Throw(c, A2_INTERNAL + 144);
	}
}


static A2_symbol *a2c_GrabSymbol(A2_compiler *c, A2_lexvalue *l)
{
	if(!a2_IsSymbol(l->token))
		a2c_Throw(c, A2_INTERNAL + 160);	/* Not a symbol! */
	if(!(l->v.sym->flags & A2_SF_TEMPORARY))
		a2c_Throw(c, A2_INTERNAL + 161);	/* Already grabbed! */
	l->v.sym->flags &= ~A2_SF_TEMPORARY;
	return l->v.sym;
}


/* Replace current token. */
static void a2c_SetToken(A2_compiler *c, int tk, int i)
{
	a2c_FreeToken(c, &c->l[0]);
	c->l[0].token = tk;
	c->l[0].v.i = i;
}

static void a2c_SetTokenf(A2_compiler *c, int tk, double f)
{
	a2c_FreeToken(c, &c->l[0]);
	c->l[0].token = tk;
	c->l[0].v.f = f;
}


/* Skip whitespace, optionally including newlines. */
static void a2c_SkipWhite(A2_compiler *c, int skipnewline)
{
	int ch;
	while(1)
	{
		switch((ch = a2_GetChar(c)))
		{
		  case '\n':
			if(!skipnewline)
				break;
			/* Fallthrough */
		  case ' ':
		  case '\t':
		  case '\r':
			continue;
		  case '/':
			switch(a2_GetChar(c))
			{
			  case '/':
				while((ch = a2_GetChar(c)) != -1)
					if(ch == '\n')
						break;
				if(ch != -1)
					a2_UngetChar(c);
				continue;
			  case '*':
			  {
				int prevch = 0;
				for( ; (ch = a2_GetChar(c)) != -1; prevch = ch)
					if((prevch == '*') && (ch == '/'))
						break;
				continue;
			  }
			}
			if(ch != -1)
				a2_UngetChar(c);
			break;
		}
		if(ch != -1)
			a2_UngetChar(c);
		return;
	}
}


#ifdef DUMPTOKENS
static int a2c_Lex2(A2_compiler *c, int whitenewline)
#else
static int a2c_Lex(A2_compiler *c, int whitenewline)
#endif
{
	char *name;
	int nstart, ch, i;
	A2_handle h;
	A2_symbol *s;

	a2c_FreeToken(c, &c->l[A2_LEXDEPTH - 1]);
	for(i = A2_LEXDEPTH - 1; i; --i)
		c->l[i] = c->l[i - 1];
	memset(&c->l[0].v, 0, sizeof(c->l[0].v));

	c->lexbufpos = 0;

	a2c_SkipWhite(c, whitenewline);
	ch = a2_GetChar(c);

	switch(ch)
	{
	  case -1:
		return (c->l[0].token = TK_EOF);
	  case ',':
		if(!c->commawarned)
		{
			fprintf(stderr, "Audiality 2: WARNING: ',' as a "
					"statement delimiter is "
					"deprecated! Please use ';' or "
					"newline.\n");
			fprintf(stderr, "Audiality 2: (No further "
					"warnings about this will be "
					"issued.)\n");
			c->commawarned = 1;
		}
		/* Fallthrough */
	  case ';':
	  case '\n':
		c->l[0].v.i = ch;
		return (c->l[0].token = TK_EOS);
	  case '"':
		return a2c_LexString(c);
	}

	/* Numeric literals */
	if(((ch >= '0') && (ch <= '9')) || (ch == '-') || (ch == '.'))
	{
		A2_errors res = a2_GetNum(c, ch, &c->l[0].v.f, 0);
		if(res == A2_OK)
			return (c->l[0].token = TK_VALUE);
		else if(res != A2_BADVALUE)
			a2c_Throw(c, res);
	}

	/* Check for valid identifiers */
	nstart = c->l[0].pos - 1;
	while(((ch >= 'a') && (ch <= 'z')) ||
			((ch >= 'A') && (ch <= 'Z')) ||
			((ch >= '0') && (ch <= '9')) ||
			(ch == '_'))
		ch = a2_GetChar(c);
	if(nstart == c->l[0].pos - 1)
		return a2c_GetOpOrChar(c, ch);
	a2_UngetChar(c);
	name = a2c_ndup(c->source + nstart, c->l[0].pos - nstart);
	DUMPLSTRINGS(fprintf(stderr, " [\"%s\":  ", name);)

	/* Try the symbol stack... */
	if((s = a2_FindSymbol(c->state, c->symbols, name)))
	{
		DUMPLSTRINGS(fprintf(stderr, "symbol %p] ", s);)
		c->l[0].token = s->token;
		if(a2_IsValue(s->token))
			c->l[0].v.f = s->v.f;
		else if(a2_IsSymbol(s->token))
			c->l[0].v.sym = s;
		else
			c->l[0].v.i = s->v.i;
		free(name);
		return c->l[0].token;	/* Symbol! */
	}

	/* Try imports... */
	if((h = a2_find_import(c, name)) >= 0)
	{
		c->l[0].token = 0;
		switch(a2_TypeOf(c->state, h))
		{
		  /* Valid types */
		  case A2_TBANK:	c->l[0].token = TK_BANK;	break;
		  case A2_TWAVE:	c->l[0].token = TK_WAVE;	break;
		  case A2_TUNIT:	c->l[0].token = TK_UNIT;	break;
		  case A2_TPROGRAM:	c->l[0].token = TK_PROGRAM;	break;
		  case A2_TSTRING:	c->l[0].token = TK_STRING;	break;
		  /* Warning eliminator - still warns if we miss a type! */
		  case A2_TDETACHED:
		  case A2_TNEWVOICE:
		  case A2_TVOICE:
		  case A2_TSTREAM:
		  case A2_TXICLIENT:
			/* Imported a dynamic object...? Can't be right. */
			free(name);
			a2c_Throw(c, A2_INTERNAL + 146);
		}
		if(c->l[0].token)
		{
			/*
			 * No deps! We rely on objects found this way to be
			 * managed by our owner, or owned by an imported bank!
			 */
			c->l[0].v.i = h;
			DUMPLSTRINGS(fprintf(stderr, "token %s (%d)] ",
					a2c_T2S(c->l[0].token), c->l[0].token);)
			free(name);
			return c->l[0].token;
		}
	}

	/* No symbol, no import! Return it as a new name. */
	if(!(s = a2_NewSymbol(name, TK_NAME)))
	{
		DUMPLSTRINGS(fprintf(stderr, "COULD NOT CREATE SYMBOL!] ");)
		free(name);
		a2c_Throw(c, A2_OOMEMORY);
	}
	DUMPLSTRINGS(fprintf(stderr, "name] ");)
	s->flags |= A2_SF_TEMPORARY;
	c->l[0].token = TK_NAME;
	c->l[0].v.sym = s;
	free(name);
	return c->l[0].token;
}

#ifdef DUMPTOKENS
static void a2c_DumpToken(A2_compiler *c, A2_lexvalue *l)
{
	if((l->token >= ' ') && (l->token <= 255))
		fprintf(stderr, "%d: '%c' (%d)", l->pos, l->token, l->token);
	else
		fprintf(stderr, "%d: %s (%d)", l->pos, a2c_T2S(l->token), l->token);
	if(l->token == TK_INSTRUCTION)
		fprintf(stderr, " %s", a2_insnames[a2c_GetIndex(c, l)]);
}
static int a2c_Lex(A2_compiler *c, int whitenewline)
{
	int i;
	a2c_Lex2(c, whitenewline);
	fprintf(stderr, " [[");
	a2c_DumpToken(c, &c->l[0]);
	fprintf(stderr, "]]");
	for(i = 1; i < A2_LEXDEPTH; ++i)
	{
		fprintf(stderr, "[");
		a2c_DumpToken(c, &c->l[i]);
		fprintf(stderr, "]");
	}
	fprintf(stderr, "\n");
	return c->l[0].token;
}
#endif

static int a2c_LexNamespace(A2_compiler *c, A2_symbol *namespace)
{
	int tk;
	A2_symbol *ssave = c->symbols;
	c->symbols = namespace;
	tk = a2c_Lex(c, 0);
	c->symbols = ssave;
	return tk;
}


static void a2c_Unlex(A2_compiler *c)
{
	int i;
	if(!c->l[0].token)
		a2c_Throw(c, A2_INTERNAL + 145);
	a2c_FreeToken(c, &c->l[0]);
	for(i = 1; i < A2_LEXDEPTH; ++i)
		c->l[i - 1] = c->l[i];
	memset(&c->l[A2_LEXDEPTH - 1], 0, sizeof(A2_lexvalue));
#ifdef DUMPTOKENS
	fprintf(stderr, " [unlex ==> [");
	a2c_DumpToken(c, &c->l[0]);
	fprintf(stderr, "]]\n");
#else
	DUMPLSTRINGS(fprintf(stderr, " [unlex]\n");)
#endif
}


/* Like a2c_Unlex() except we don't move the source position back for relex. */
static void a2c_DropToken(A2_compiler *c)
{
	int pos = c->l[0].pos;
	DUMPLSTRINGS(fprintf(stderr, " [drop]\n");)
	a2c_Unlex(c);
	c->l[0].pos = pos;
}


/*---------------------------------------------------------
	VM register allocation
---------------------------------------------------------*/

static unsigned a2c_AllocReg(A2_compiler *c)
{
	int r;
	for(r = 0; r < A2_REGISTERS; ++r)
		if(!c->regmap[r])
		{
			c->regmap[r] = 1;
			REGDBG(fprintf(stderr, "[AllocReg %d]\n", r);)
			return r;
		}
	a2c_Throw(c, A2_OUTOFREGS);
}


static void a2c_FreeReg(A2_compiler *c, unsigned r)
{
	REGDBG(fprintf(stderr, "[FreeReg %d]\n", r);)
#ifdef DEBUG
	if(!c->regmap[r])
	{
		fprintf(stderr, "Audiality 2 INTERNAL ERROR: Tried to free "
				"unused VM register R%d!\n", r);
		a2c_Throw(c, A2_INTERNAL + 100);
	}
#endif
	c->regmap[r] = 0;
}


/*---------------------------------------------------------
	Scope management
---------------------------------------------------------*/

typedef struct A2_scope
{
	A2_symbol	*symbols;
	A2_regmap	regmap;
	int		canexport;
} A2_scope;

static void a2c_BeginScope(A2_compiler *c, A2_scope *sc)
{
	sc->symbols = c->symbols;
	memcpy(sc->regmap, c->regmap, sizeof(A2_regmap));
	sc->canexport = c->canexport;
	c->canexport = 0;	/* Only top level scope can export normally! */
}

static void a2c_EndScope(A2_compiler *c, A2_scope *sc)
{
	int res = A2_OK;
	A2_nametab *x = &c->target->exports;
	memcpy(c->regmap, sc->regmap, sizeof(A2_regmap));

	DBG(fprintf(stderr, "=== end scope ===\n");)
	while(c->symbols != sc->symbols)
	{
		A2_symbol *s = c->symbols;
		A2_handle h;
		c->symbols = s->next;
		if(s->token == TK_FWDECL)
			res = A2_UNDEFSYM;
		DBG(fprintf(stderr, "   %s\t", s->name);)
		switch(s->token)
		{
		  case TK_BANK:
		  case TK_WAVE:
		  case TK_UNIT:
		  case TK_PROGRAM:
		  case TK_STRING:
			h = s->v.i;
			DBG(fprintf(stderr, "h: %d\t", h);)
			DBG(fprintf(stderr, "t: %s\t", a2_TypeName(c->state,
					a2_TypeOf(c->state, h)));)
			break;
		  default:
			h = -1;
			DBG(fprintf(stderr, "(unsupported)\t");)
			break;
		}
		DBG(
			if(s->flags & A2_SF_EXPORTED)
				fprintf(stderr, "EXPORTED\n");
			else
				fprintf(stderr, "\n");
		)
		if(s->flags & A2_SF_EXPORTED)
		{
#ifdef DEBUG
			if(!c->canexport)
			{
				fprintf(stderr, "Trying to export symbol "
						"\"%s\" from a context where "
						"exports are not allowed!\n",
						s->name);
				a2c_Throw(c, A2_INTERNAL + 120);
			}
#endif
			if(h >= 0)
				a2nt_AddItem(x, s->name, h);
		}
		a2_FreeSymbol(s);
	}
	DBG(fprintf(stderr, "=================\n");)
	if(res)
		a2c_Throw(c, res);
	c->canexport = sc->canexport;
}

/* Like a2r_EndScope(), except we don't check or export anything! */
static void a2c_CleanScope(A2_compiler *c, A2_scope *sc)
{
	int i;
	memcpy(c->regmap, sc->regmap, sizeof(A2_regmap));
	for(i = 0; i < A2_LEXDEPTH; ++i)
		a2c_FreeToken(c, &c->l[i]);
	memset(c->l, 0, sizeof(c->l));
	while(c->symbols != sc->symbols)
	{
		A2_symbol *s = c->symbols;
		c->symbols = s->next;
		a2_FreeSymbol(s);
	}
	c->canexport = sc->canexport;
}


/*---------------------------------------------------------
	Parser
---------------------------------------------------------*/

static void a2c_Expect(A2_compiler *c, A2_tokens tk, A2_errors err)
{
	if(a2c_Lex(c, 0) != tk)
		a2c_Throw(c, err);
}


/* Return the value of the next token, which is required to be a TK_VALUE. */
static double a2c_Value(A2_compiler *c)
{
	a2c_Expect(c, TK_VALUE, A2_EXPVALUE);
	return a2c_GetValue(c, &c->l[0]);
}


/*
 * Generate branch based on 'op' and the current lexer token. The target
 * position is specified via 'to', which can be A2_UNDEFJUMP if the position is
 * not yet known. If not NULL, 'fixpos' receives the position of the issued
 * branch instruction.
 */
static void a2c_Branch(A2_compiler *c, A2_opcodes op, unsigned to, int *fixpos)
{
	int r;
	if(a2_IsValue(c->l[0].token))
	{
		/* FIXME:
		 *	These are no conditionals! They should be evaluated
		 *	at compile time instead.
		 */
		r = a2c_AllocReg(c);
		a2c_Codef(c, OP_LOAD, r, a2c_GetValue(c, &c->l[0]));
		if(fixpos)
			*fixpos = c->coder->pos;
		a2c_Code(c, op, r, to);
		a2c_FreeReg(c, r);
	}
	else if(a2_IsRegister(c->l[0].token))
	{
		r = a2c_GetIndex(c, &c->l[0]);
		if(fixpos)
			*fixpos = c->coder->pos;
		a2c_Code(c, op, r, to);
		if(c->l[0].token == TK_TEMPREG)
			a2c_FreeReg(c, r);
	}
	else
		a2c_Throw(c, A2_INTERNAL + 101);
}


static void a2c_VarDecl(A2_compiler *c, A2_symbol *s)
{
	s->token = TK_REGISTER;
	s->v.i = a2c_AllocReg(c);
	a2_PushSymbol(&c->symbols, s);
}


/* Evaluate unary operator on a constant value. */
static double a2c_DoUnop(A2_compiler *c, A2_opcodes op, double v)
{
	switch(op)
	{
	  case OP_P2DR:
		return 1000.0f / (powf(2.0f, v) * A2_MIDDLEC);
	  case OP_NEGR:
		return -v;
	  case OP_NOTR:
		return v ? 0.0f : 1.0f;
	  default:
		a2c_Throw(c, A2_INTERNAL + 150);
	}
}


/* Evaluate binary operator on constant values. */
static double a2c_DoOp(A2_compiler *c, A2_opcodes op, double vl, double vr)
{
	switch(op)
	{
	  case OP_MOD:
		if(!vr)
			a2c_Throw(c, A2_DIVBYZERO);
		return fmod(vl, vr);
	  case OP_ADD:
		return vl + vr;
	  case OP_MUL:
		return vl * vr;
	  case OP_QUANT:
		if(!vr)
			a2c_Throw(c, A2_DIVBYZERO);
		return floor(vl / vr) * vr;
	  case OP_SUBR:
		return vl - vr;
	  case OP_DIVR:
		if(!vr)
			a2c_Throw(c, A2_DIVBYZERO);
		return vl / vr;
	  case OP_GR:
		return vl > vr ? 1.0f : 0.0f;
	  case OP_LR:
		return vl < vr ? 1.0f : 0.0f;
	  case OP_GER:
		return vl >= vr ? 1.0f : 0.0f;
	  case OP_LER:
		return vl <= vr ? 1.0f : 0.0f;
	  case OP_EQR:
		return vl == vr ? 1.0f : 0.0f;
	  case OP_NER:
		return vl != vr ? 1.0f : 0.0f;
	  case OP_ANDR:
		return vl && vr ? 1.0f : 0.0f;
	  case OP_ORR:
		return vl || vr ? 1.0f : 0.0f;
	  case OP_XORR:
		return !vl != !vr ? 1.0f : 0.0f;
	  default:
		a2c_Throw(c, A2_INTERNAL + 151);
	}
}


/* Issue code to perform an operation on registers. */
static void a2c_code_op_r(A2_compiler *c, A2_opcodes op, int to, unsigned r)
{
	switch(op)
	{
	  case OP_ADD:
	  case OP_MUL:
	  case OP_MOD:
	  case OP_QUANT:
	  case OP_RAND:
	  case OP_LOAD:
	  case OP_SIZEOF:
		a2c_Code(c, op + 1, to, r);
		break;
	  case OP_DELAY:
	  case OP_TDELAY:
	  case OP_DEBUG:
		a2c_Code(c, op + 1, r, 0);
		break;
	  case OP_SUBR:
	  case OP_DIVR:
	  case OP_P2DR:
	  case OP_NEGR:
/*TODO: These should go in the first group when we add the immediate versions! */
	  case OP_GR:
	  case OP_LR:
	  case OP_GER:
	  case OP_LER:
	  case OP_EQR:
	  case OP_NER:
/*TODO: / */
	  case OP_ANDR:
	  case OP_ORR:
	  case OP_XORR:
	  case OP_NOTR:
		a2c_Code(c, op, to, r);
		break;
	  default:
		a2c_Throw(c, A2_BADOPCODE);
	}
}

/* Issue code to perform an operation involving a constant value. */
static void a2c_code_op_v(A2_compiler *c, A2_opcodes op, int to, double v)
{
	int tmpr = to;
	switch(op)
	{
	  case OP_MOD:
	  case OP_QUANT:
		if(!v)
			a2c_Throw(c, A2_DIVBYZERO);
	  case OP_ADD:
	  case OP_MUL:
	  case OP_RAND:
	  case OP_LOAD:
	  case OP_DELAY:	/* ('to' is not used by these last three) */
	  case OP_TDELAY:
	  case OP_DEBUG:
		a2c_Codef(c, op, to, v);
		break;
	  case OP_SUBR:
		a2c_Codef(c, OP_ADD, to, -v);
		break;
	  case OP_DIVR:
		if(!v)
			a2c_Throw(c, A2_DIVBYZERO);
		a2c_Codef(c, OP_MUL, to, 1.0f / v);
		break;
	  default:
		switch(op)
		{
		  /* In-place unary operators - no teporary register needed! */
		  case OP_RAND:
		  case OP_P2DR:
		  case OP_NEGR:
		  case OP_NOTR:
			break;
		  default:
			tmpr = a2c_AllocReg(c);
			break;
		}
		a2c_Codef(c, OP_LOAD, tmpr, v);
		a2c_code_op_r(c, op, to, tmpr);
		if(tmpr != to)
			a2c_FreeReg(c, tmpr);
		break;
	}
}

/* Issue code to perform operation on a handle. */
static void a2c_code_op_h(A2_compiler *c, A2_opcodes op, int to, unsigned h)
{
	switch(op)
	{
	  case OP_SIZEOF:
		a2c_Code(c, op, to, h);
		break;
	  case OP_LOAD:
		a2c_Code(c, op, to, h << 16);
		break;
	  default:
		a2c_Throw(c, A2_INTERNAL + 105);
	}
}

/* Issue code '<op> <to> <from>', where <from> is derived lex state 'l' */
static void a2c_CodeOpL(A2_compiler *c, A2_opcodes op, int to, A2_lexvalue *l)
{
	if(a2_IsRegister(l->token))
		a2c_code_op_r(c, op, to, a2c_GetIndex(c, l));
	else if(a2_IsHandle(l->token))
		a2c_code_op_h(c, op, to, a2c_GetHandle(c, l));
	else if(a2_IsValue(l->token))
		a2c_code_op_v(c, op, to, a2c_GetValue(c, l));
	else
		a2c_Throw(c, A2_INTERNAL + 102);
}


/* Returns 1 if 'op' the opcode of a binary operator VM instruction */
static int a2c_IsBinOp(A2_opcodes op)
{
	switch(op)
	{
	  case OP_MOD:
	  case OP_ADD:
	  case OP_MUL:
	  case OP_QUANT:
	  case OP_SUBR:
	  case OP_DIVR:
	  case OP_GR:
	  case OP_LR:
	  case OP_GER:
	  case OP_LER:
	  case OP_EQR:
	  case OP_NER:
	  case OP_ANDR:
	  case OP_ORR:
	  case OP_XORR:
		return 1;
	  default:
		return 0;
	}
}


static void a2c_SimplExp(A2_compiler *c, int r);

/*
 * Expression, terminated with the token specified by 'delim'
 * 
 * 'r' works the same way as with a2c_SimplExp().
 */
static void a2c_Expression(A2_compiler *c, int r, int delim)
{
	int res_tk = TK_REGISTER;

	a2c_SimplExp(c, r);

	/*
	 * NOTE:
	 *	It's not theoretically incorrect for an expression to return a
	 *	handle, but we don't have any typed operators that can generate
	 *	handles at this point.
	 */
	if(a2_IsHandle(c->l[0].token))
		a2c_Throw(c, A2_NEXPHANDLE); /* No arithmetics on these! */

	while(1)
	{
		int op;
		A2_lexvalue lopr;
		switch(a2c_Lex(c, 1))
		{
		  case TK_INSTRUCTION:
			op = a2c_GetIndex(c, &c->l[0]);
			if(!a2c_IsBinOp(op))
				a2c_Throw(c, A2_EXPBINOP);
			break;

		  /* Immediate/register operator instruction pairs */
		  case '+':		op = OP_ADD;	break;
		  case '*':		op = OP_MUL;	break;
		  case '%':		op = OP_MOD;	break;

		  /* Operator instructions with register versions only */
		  case '-':		op = OP_SUBR;	break;
		  case '/':		op = OP_DIVR;	break;
		  case '>':		op = OP_GR;	break;
		  case '<':		op = OP_LR;	break;

		  case TK_GE:		op = OP_GER;	break;
		  case TK_LE:		op = OP_LER;	break;
		  case TK_EQ:		op = OP_EQR;	break;
		  case TK_NE:		op = OP_NER;	break;

		  case KW_AND:		op = OP_ANDR;	break;
		  case KW_OR:		op = OP_ORR;	break;
		  case KW_XOR:		op = OP_XORR;	break;

		  default:
			/* We're either done, or there's a parse error! */
			if(c->l[0].token != delim)
				a2c_Throw(c, A2_EXPOP);
			/*
			 * Drop the delimiter token, leaving the previous token
			 * as our final result!
			 */
			a2c_DropToken(c);
			return;
		}

		/*
		 * Grab the left operand, as further parsing may recursively
		 * mess up the lexer stack!
		 */
		lopr = c->l[1];

		/* Parse right hand operand */
		a2c_SkipWhite(c, 1);
		a2c_SimplExp(c, -1);
		if(a2_IsHandle(c->l[0].token))
			a2c_Throw(c, A2_NEXPHANDLE);

		/* If both operands are constant, we evaluate compile time! */
		if((lopr.token == TK_VALUE) && (c->l[0].token == TK_VALUE))
		{
			a2c_SetTokenf(c, TK_VALUE, a2c_DoOp(c, op,
					a2c_GetValue(c, &lopr),
					a2c_GetValue(c, &c->l[0])));
			continue;
		}

		/*
		 * Right... We need to issue some code. First, make sure we
		 * have a target register:
		 *	1. If one was provided by the caller, use that.
		 *	2. If the left operand is a temporary reg, grab that!
		 *	3. Allocate a temporary register.
		 */
		if(r < 0)
		{
			if(lopr.token == TK_TEMPREG)
				r = a2c_GetIndex(c, &lopr);
			else
				r = a2c_AllocReg(c);
			res_tk = TK_TEMPREG;
		}

		/* We're not supposed to overwrite the right hand operand! */
		if(a2_IsRegister(c->l[0].token) &&
				(a2c_GetIndex(c, &c->l[0]) == r))
			a2c_Throw(c, A2_INTERNAL + 153);

		/* Ensure that the left hand operand is in a register. */
		a2c_CodeOpL(c, OP_LOAD, r, &lopr);
		if((lopr.token == TK_TEMPREG) &&
				(a2c_GetIndex(c, &lopr) != r))
			a2c_FreeReg(c, a2c_GetIndex(c, &lopr));

		/* Code! */
		a2c_CodeOpL(c, op, r, &c->l[0]);

		/*
		 * If the right hand operand was in a temporary register, we're
		 * responsible for freeing that.
		 */
		if(c->l[0].token == TK_TEMPREG)
			a2c_FreeReg(c, a2c_GetIndex(c, &c->l[0]));

		/* Leave the result as the new current token. */
		a2c_SetToken(c, res_tk, r);
	}
}


/*
 * Variable, that is, a named register, including:
 *	* Plain local variables
 *	* Hardwired VM registers
 *	* Unit control registers
 *	* Program entry point arguments
 *	* Local function arguments
 *
 * Returns the register index, and with the register in the lexer state, or
 * fails with an exception.
 */
static int a2c_Variable(A2_compiler *c)
{
	switch(a2c_Lex(c, 0))
	{
	  case TK_REGISTER:
		return a2c_GetIndex(c, &c->l[0]);
	  case TK_NAMESPACE:
	  {
		A2_symbol *ns = c->l[0].v.sym->symbols;
		a2c_Expect(c, '.', A2_NEXPTOKEN);
		if(a2c_LexNamespace(c, ns) != TK_REGISTER)
			a2c_Throw(c, A2_EXPVARIABLE);
		return a2c_GetIndex(c, &c->l[0]);
	  }
	  default:
		a2c_Throw(c, A2_EXPVARIABLE);
	}
}


/*
 * Parse value or variable, or issue code to evaluate expression. Returns with
 * the result as the current token.
 *
 * If the expression issues code that can target a register, 'r' is used.
 *
 * If 'r' is -1, a temporary register is allocated, and the token is set to
 * TK_TEMPREG. In this case, the caller is responsible for freeing the
 * temporary register!
 *
 * NOTE:
 *	'r', whether -1 or a specific register index, is ONLY used if code has
 *	been issued! Static objects, variables etc are just returned as is, as
 *	are the results of constant expression evaluation.
 */
static void a2c_SimplExp(A2_compiler *c, int r)
{
	switch(a2c_Lex(c, 0))
	{
	  case TK_VALUE:
	  case TK_WAVE:
	  case TK_PROGRAM:
	  case TK_STRING:
	  case TK_LABEL:
		return;		/* Done! Just return as is - no code! */
	  case '(':
		a2c_Expression(c, r, ')');
		return;
	  case TK_INSTRUCTION:
	  {
		/* Unary operator */
		int tmpr = r;
		int op = a2c_GetIndex(c, &c->l[0]);
		switch(op)
		{
		  case OP_P2DR:
		  case OP_RAND:
		  case OP_NEGR:
		  case OP_NOTR:
		  case OP_SIZEOF:
			break;
		  default:
			a2c_Throw(c, A2_NOTUNARY);
		}
		a2c_SimplExp(c, r);
		if(c->l[0].token == TK_VALUE)
		{
			/* Constant expression? */
			switch(op)
			{
			  case OP_P2DR:
			  case OP_NEGR:
			  case OP_NOTR:
				a2c_SetTokenf(c, TK_VALUE, a2c_DoUnop(c, op,
						a2c_GetValue(c, &c->l[0])));
				return;
			}
		}
		if((r < 0) && (c->l[0].token != TK_TEMPREG))
			tmpr = a2c_AllocReg(c);
		a2c_CodeOpL(c, op, tmpr, &c->l[0]);
		a2c_SetToken(c, r < 0 ? TK_TEMPREG : TK_REGISTER, tmpr);
		return;
	  }
	  default:
		a2c_Unlex(c);
		a2c_Variable(c);
		return;
	}
}


static void a2c_Arguments(A2_compiler *c, int maxargc)
{
	int argc;
	for(argc = 0; argc <= maxargc; ++argc)
	{
		a2c_Lex(c, 0);
		if((c->l[0].token == '}') || (c->l[0].token == TK_EOS))
		{
			a2c_Unlex(c);
			return;		/* Done! */
		}
		a2c_Unlex(c);
		a2c_SimplExp(c, -1);
		if(a2_IsValue(c->l[0].token))
			a2c_Codef(c, OP_PUSH, 0, a2c_GetValue(c, &c->l[0]));
		else if(a2_IsHandle(c->l[0].token))
			a2c_Code(c, OP_PUSH, 0,
					a2c_GetHandle(c, &c->l[0]) << 16);
		else if(a2_IsRegister(c->l[0].token))
		{
			int r = a2c_GetIndex(c, &c->l[0]);
			a2c_Code(c, OP_PUSHR, r, 0);
			if(c->l[0].token == TK_TEMPREG)
				a2c_FreeReg(c, r);
		}
		else
			/* Shouldn't happen: a2c_SimplExp() should fail! */
			a2c_Throw(c, A2_INTERNAL + 112);
	}
	a2c_Throw(c, A2_MANYARGS);
}


static int a2c_ConstArguments(A2_compiler *c, int maxargc, int *argv)
{
	int argc;
	for(argc = 0; argc <= maxargc; ++argc)
	{
		a2c_Lex(c, 0);
		if((c->l[0].token == '}') || (c->l[0].token == TK_EOS))
		{
			a2c_Unlex(c);
			return argc;	/* Done! */
		}
		a2c_Unlex(c);
/*
TODO: Readable attributes in wavedefs, so we can pass them as arguments to
TODO: the rendered program!
*/
		a2c_SimplExp(c, -1);
		if(a2_IsValue(c->l[0].token))
			argv[argc] = a2c_Num2VM(c, a2c_GetValue(c, &c->l[0]));
		else if(a2_IsHandle(c->l[0].token))
			argv[argc] = a2c_GetHandle(c, &c->l[0]) << 16;
		else
			a2c_Throw(c, A2_EXPCONSTANT);
	}
	a2c_Throw(c, A2_MANYARGS);
}


static void a2c_Instruction(A2_compiler *c, A2_opcodes op, int r)
{
	int i, p;
	switch(op)
	{
	  case OP_END:
	  case OP_RETURN:
		a2c_Code(c, op, 0, 0);
		break;
	  case OP_WAKE:
	  case OP_FORCE:
		if(!c->inhandler)
			a2c_Throw(c, A2_NOWAKEFORCE);
	  case OP_JUMP:
		a2c_Lex(c, 0);
		if((c->l[0].token != TK_LABEL) && (c->l[0].token != TK_FWDECL))
			a2c_Throw(c, A2_EXPLABEL);
		a2c_Code(c, op, 0, a2c_GetIndex(c, &c->l[0]));
		break;
	  case OP_LOOP:
		r = a2c_Variable(c);
		a2c_Expect(c, TK_LABEL, A2_EXPLABEL);
		a2c_Code(c, op, r, a2c_GetIndex(c, &c->l[0]));
		break;
	  case OP_JZ:
	  case OP_JNZ:
	  case OP_JG:
	  case OP_JL:
	  case OP_JGE:
	  case OP_JLE:
		a2c_SimplExp(c, -1);
		a2c_Expect(c, TK_LABEL, A2_EXPLABEL);
		i = a2c_GetIndex(c, &c->l[0]);
		a2c_DropToken(c);
		a2c_Branch(c, op, i, NULL);
		break;
	  case OP_SPAWN:
	  case OP_SPAWNV:
	  case OP_SPAWND:
		switch(a2c_Lex(c, 0))
		{
		  case TK_REGISTER:
			++op;
			p = a2c_GetIndex(c, &c->l[0]);
			i = A2_MAXARGS;	/* Can't check these compile time... */
			break;
		  case TK_PROGRAM:
			p = a2c_GetHandle(c, &c->l[0]);	/* Program handle */
			i = (a2_GetProgram(c->state, p))->funcs[0].argc;
			break;
		  default:
			a2c_Throw(c, A2_EXPPROGRAM);
		}
		a2c_Arguments(c, i);
		a2c_Code(c, op, r, p);
		break;
	  case OP_CALL:
		a2c_Expect(c, TK_FUNCTION, A2_EXPFUNCTION);
		p = a2c_GetIndex(c, &c->l[0]);	/* Function entry point */
		if(p >= c->coder->program->nfuncs)
			a2c_Throw(c, A2_BADENTRY); /* Invalid entry point! */
		i = c->coder->program->funcs[p].argc;
		a2c_Arguments(c, i);
		a2c_Code(c, op, r, p);
		break;
	  case OP_WAIT:
		if(c->inhandler)
			a2c_Throw(c, A2_NORUN);
		a2c_Code(c, op, a2c_Num2Int(c, a2c_Value(c)), 0);
		break;
	  case OP_SEND:
	  case OP_SENDR:
	  case OP_SENDS:
	  case OP_SENDA:
		p = a2c_Num2Int(c, a2c_Value(c));	/* Entry point */
		if(!p)
			a2c_Throw(c, A2_BADENTRY); /* 0 is not for messages! */
		a2c_Arguments(c, A2_MAXARGS);
		a2c_Code(c, op, r, p);
		break;
	  case OP_KILL:
		if(a2c_Lex(c, 0) == '*')
			a2c_Code(c, OP_KILLA, 0, 0);
		else
		{
			a2c_Unlex(c);
			a2c_SimplExp(c, -1);
			if(a2_IsValue(c->l[0].token))
				a2c_Code(c, OP_KILL,
					a2c_Num2Int(c, a2c_GetValue(c,
					&c->l[0])), 0);
			else if(a2_IsRegister(c->l[0].token))
			{
				r = a2c_GetIndex(c, &c->l[0]);
				a2c_Code(c, OP_KILLR, r, 0);
				if(c->l[0].token == TK_TEMPREG)
					a2c_FreeReg(c, r);
			}
			else
				a2c_Throw(c, A2_INTERNAL + 113);
		}
		break;
	  case OP_SET:
		a2c_Code(c, OP_SET, a2c_Variable(c), 0);
		break;
	  case OP_SETALL:
		a2c_Code(c, OP_SETALL, 0, 0);
		break;
	  case OP_DELAY:
	  case OP_TDELAY:
		if(c->inhandler)
			a2c_Throw(c, A2_NOTIMING);
		/* Fall through! */
	  case OP_DEBUG:
		a2c_SimplExp(c, -1);
		a2c_CodeOpL(c, op, 0, &c->l[0]);
		if(c->l[0].token == TK_TEMPREG)
			a2c_FreeReg(c, a2c_GetIndex(c, &c->l[0]));
		break;
	  case OP_ADD:
	  case OP_SUBR:
	  case OP_MUL:
	  case OP_DIVR:
	  case OP_MOD:
	  case OP_QUANT:
	  case OP_RAND:
	  case OP_P2DR:
	  case OP_NEGR:
	  case OP_NOTR:
	  case OP_SIZEOF:
		if(a2c_Lex(c, 0) == '!')
		{
			A2_symbol *s;
			if((op != OP_RAND) && (op != OP_P2DR) &&
					(op != OP_NEGR) && (op != OP_NOTR))
				a2c_Throw(c, A2_BADVARDECL);
			a2c_Expect(c, TK_NAME, A2_EXPNAME);
			s = a2c_GrabSymbol(c, &c->l[0]);
			a2c_VarDecl(c, s);
			r = s->v.i;
		}
		else
		{
			a2c_Unlex(c);
			r = a2c_Variable(c);
		}
		a2c_SimplExp(c, (op == OP_RAND) || (op == OP_P2DR) ||
				(op == OP_NEGR) || (op == OP_NOTR) ? r : -1);
		a2c_CodeOpL(c, op, r, &c->l[0]);
		if(c->l[0].token == TK_TEMPREG)
			a2c_FreeReg(c, a2c_GetIndex(c, &c->l[0]));
		break;
	  default:
		a2c_Throw(c, A2_BADOPCODE);
	}
}


static void a2c_Import(A2_compiler *c, int export)
{
	int h, res;
	const char *name;
	a2c_Expect(c, TK_STRING, A2_EXPSTRING);
	name = a2_String(c->state, c->l[0].v.i);
	if(c->path)
	{
		/* Try the directory of the current file first! */
		int bufsize = strlen(c->path) + 1 + strlen(name) + 1;
		char *buf = malloc(bufsize);
		if(!buf)
		{
			a2_Release(c->state, c->l[0].v.i);
			a2c_Throw(c, -A2_OOMEMORY);
		}
		snprintf(buf, bufsize, "%s/%s", c->path, name);
		buf[bufsize - 1] = 0;
		h = a2_Load(c->state, buf);
		free(buf);
		switch(-h)
		{
		  case A2_OPEN:
		  case A2_READ:
			h = a2_Load(c->state, name);
			break;
		  default:
			/*
			 * If we get here, we most likely got the right file,
			 * but it failed to compile, so we're not going to try
			 * another location!
			 */
			break;
		}
	}
	else
		h = a2_Load(c->state, name);
	if(h < 0)
	{
		fprintf(stderr, "Could not import \"%s\"! (%s)\n",
				name, a2_ErrorString(-h));
		a2_Release(c->state, c->l[0].v.i);
		a2c_Throw(c, -h);
	}
	a2_Release(c->state, c->l[0].v.i);
	if(((res = a2ht_AddItem(&c->target->deps, h)) < 0) ||
			((res = a2ht_AddItem(&c->imports, h)) < 0))
	{
		a2_Release(c->state, h);
		a2c_Throw(c, -res);
	}
}


static void a2c_Def(A2_compiler *c, int export)
{
	A2_symbol *s;

	/* Local? */
	if(a2c_Lex(c, 0) == '.')
		export = 0;
	else
		a2c_Unlex(c);

	a2c_Expect(c, TK_NAME, A2_EXPNAME);
	s = a2c_GrabSymbol(c, &c->l[0]);

	/*
	 * A bit ugly; we just ignore the 'export' argument if we're in a
	 * scope from where symbols cannot be exported...
	 */
	if(c->canexport && (export || c->exportall))
		s->flags |= A2_SF_EXPORTED;

	a2c_SimplExp(c, -1);
	switch(c->l[0].token)
	{
	  case TK_VALUE:
		s->token = TK_VALUE;
		s->v.f = a2c_GetValue(c, &c->l[0]);
		break;
	  case TK_REGISTER:
		s->flags &= ~A2_SF_EXPORTED; /* In case we have 'exportall' */
		if(export)
			a2c_Throw(c, A2_NOEXPORT);
		/* Fall through! */
	  case TK_WAVE:
	  case TK_PROGRAM:
	  case TK_STRING:
		s->token = c->l[0].token;
		s->v.i = a2c_GetHandle(c, &c->l[0]);
		break;
	  default:
		a2c_Throw(c, A2_BADVALUE);
	}
	a2_PushSymbol(&c->symbols, s);
}


static void a2c_Body(A2_compiler *c, A2_tokens terminator);


static void a2c_ArgList(A2_compiler *c, A2_function *fn)
{
	A2_symbol *s;
	int nextr;
	uint8_t *argc = &fn->argc;
	fn->argv = nextr = a2c_AllocReg(c);
	a2c_FreeReg(c, nextr);
	for(*argc = 0; a2c_Lex(c, 1) != ')'; ++*argc)
	{
		if(*argc > A2_MAXARGS)
			a2c_Throw(c, A2_MANYARGS);
		if(c->l[0].token != TK_NAME)
			a2c_Throw(c, A2_EXPNAME);
		s = a2c_GrabSymbol(c, &c->l[0]);
		a2c_VarDecl(c, s);
		/* Make sure we don't get holes in the argument list...! */
		if(s->v.i != nextr)
			a2c_Throw(c, A2_INTERNAL + 170);
		++nextr;
		if(a2c_Lex(c, 0) == '=')
		{
			int v;
			if(a2_IsValue(a2c_Lex(c, 0)))
				v = a2c_Num2VM(c, a2c_GetValue(c, &c->l[0]));
			else if(a2_IsHandle(c->l[0].token))
				v = a2c_GetHandle(c, &c->l[0]) << 16;
			else
				a2c_Throw(c, A2_EXPVALUEHANDLE);
			fn->argdefs[*argc] = v;
		}
		else
			a2c_Unlex(c);
	}
}


static int a2c_AddUnit(A2_compiler *c, A2_symbol **namespace,
		const A2_unitdesc *ud, unsigned inputs, unsigned outputs)
{
	int i, ind;
	A2_structitem *ni = (A2_structitem *)calloc(1, sizeof(A2_structitem));
	if(!ni)
		a2c_Throw(c, A2_OOMEMORY);

	/* Add unit to program */
	ni->unitdesc = ud;
	ni->ninputs = inputs;
	ni->noutputs = outputs;
	if(c->coder->program->structure)
	{
		/* Attach as last unit */
		A2_structitem *li = c->coder->program->structure;
		for(ind = 0; li->next; li = li->next)
			++ind;
		li->next = ni;
	}
	else
	{
		ind = 0;
		c->coder->program->structure = ni;	/* First! */
	}
	ni->next = NULL;
	DUMPSTRUCT(fprintf(stderr, "  %s %d %d [", ud->name, inputs, outputs);)

	/* Allocate control registers and add them to the namespace! */
	if(!namespace)
		namespace = &c->symbols;
	for(i = 0; ud->registers[i].name; ++i)
	{
		A2_symbol *s;
		if(a2_FindSymbol(c->state, *namespace, ud->registers[i].name))
			a2c_Throw(c, A2_SYMBOLDEF);
		if(!(s = a2_NewSymbol(ud->registers[i].name, TK_REGISTER)))
			a2c_Throw(c, A2_OOMEMORY);
		s->v.i = a2c_AllocReg(c);
		a2_PushSymbol(namespace, s);
		DUMPSTRUCT(fprintf(stderr, " %s:R%d", s->name, s->v.i);)
	}
	DUMPSTRUCT(fprintf(stderr, " ]\n");)
	return ind;
}


/* Create and push a namespace and return the head of its local symbol stack */
static A2_symbol **a2c_CreateNamespace(A2_compiler *c, const char *name,
		A2_tokens kind)
{
	A2_symbol *s = a2_NewSymbol(name, TK_NAMESPACE);
	if(!s)
		a2c_Throw(c, A2_OOMEMORY);
	s->v.i = kind;
	a2_PushSymbol(&c->symbols, s);
	return &s->symbols;
}


static int a2c_IOSpec(A2_compiler *c, int min, int max, int outputs)
{
	switch(a2c_Lex(c, 0))
	{
	  case TK_VALUE:
	  {
		int val = a2c_Num2Int(c, a2c_GetValue(c, &c->l[0]));
		if(val < min || val > max)
			a2c_Throw(c, A2_VALUERANGE);
		return val;
	  }
	  case '*':
		if(!max)
			a2c_Throw(c, outputs ? A2_CANTOUTPUT : A2_CANTINPUT);
		return A2_IO_MATCHOUT;
	  case '>':
		if(!outputs)
			a2c_Throw(c, A2_NOTOUTPUT);
		if(!max)
			a2c_Throw(c, outputs ? A2_CANTOUTPUT : A2_CANTINPUT);
		return A2_IO_WIREOUT;
	  default:
		a2c_Unlex(c);
		return A2_IO_DEFAULT;	/* Not an I/O spec! Use defaults. */
	}
}


/* 'unit' statement (for a2c_StructStatement) */
static void a2c_UnitSpec(A2_compiler *c)
{
	int inputs, outputs;
	A2_symbol **namespace = NULL;
	const A2_unitdesc *ud = a2_GetUnit(c->state, a2c_GetHandle(c, &c->l[0]));
	if(!ud)
		a2c_Throw(c, A2_INTERNAL + 107); /* Object missing!? */
	switch(a2c_Lex(c, 0))
	{
	  case TK_NAME:
		/* Named unit! Put the control registers in a namespace. */
		namespace = a2c_CreateNamespace(c, c->l[0].v.sym->name, TK_UNIT);
		break;
	  default:
		/* Anonymous unit: Control registers --> current namespace. */
		a2c_Unlex(c);
		break;
	}
	inputs = a2c_IOSpec(c, ud->mininputs, ud->maxinputs, 0);
	outputs = a2c_IOSpec(c, ud->minoutputs, ud->maxoutputs, 1);
	a2c_AddUnit(c, namespace, ud, inputs, outputs);
}

/* 'wire' statement (for a2c_StructStatement) */
static void a2c_WireSpec(A2_compiler *c)
{
//TODO: wire <from_unit> <from_output> <to_unit> <to_input>
	a2c_Throw(c, A2_INTERNAL + 111);
}

static int a2c_StructStatement(A2_compiler *c, A2_tokens terminator)
{
	switch(a2c_Lex(c, 0))
	{
	  case TK_UNIT:
		a2c_UnitSpec(c);
		break;
	  case KW_WIRE:
		a2c_WireSpec(c);
		break;
	  case TK_EOS:
		return 1;
	  default:
		if(c->l[0].token != terminator)
			a2c_Throw(c, A2_NEXPTOKEN);
		return 0;
	}
	if(a2c_Lex(c, 0) == TK_EOS)
		return 1;
	if(c->l[0].token != terminator)
		a2c_Throw(c, A2_EXPEOS);
	return 0;
}


/* Check if 'si' or any other items down the chain have inputs. */
static int downstream_inputs(A2_structitem *si)
{
	while(si)
	{
		/*
		 * NOTE: ninputs is already checked against unit min/max, so we
		 *       can only ever get values that mean "there are inputs",
		 *	 and of course, 0.
		 */
		if(si->ninputs)
			return 1;
		si = si->next;
	}
	return 0;
}

static void a2c_StructDef(A2_compiler *c)
{
	A2_program *p = c->coder->program;
	int matchout = 0;
	int chainchannels = 0;	/* Number of channels in current chain */
	A2_structitem *si;
	if(a2c_Lex(c, 1) != KW_STRUCT)
	{
		a2c_Unlex(c);
		return;
	}
	DUMPSTRUCT(fprintf(stderr, "struct {\n");)
	a2c_Expect(c, '{', A2_EXPBODY);
	while(a2c_StructStatement(c, '}'))
		;
	DUMPSTRUCT(fprintf(stderr, "}\n");)

	/* Finalize the voice structure; autowiring etc... */
	DUMPSTRUCT(fprintf(stderr, "Wiring...");)
	for(si = p->structure; si; si = si->next)
	{
		DUMPSTRUCT(fprintf(stderr, " (chain %d)", chainchannels);)
		DUMPSTRUCT(fprintf(stderr, " [%s ", si->unitdesc->name);)

		/* Is this the 'inline' unit? */
		if(si->unitdesc == &a2_inline_unitdesc)
		{
			if(p->vflags & A2_SUBINLINE)
				a2c_Throw(c, A2_MULTIINLINE);
			p->vflags |= A2_SUBINLINE;
		}

		/* Autowire inputs */
		switch(si->ninputs)
		{
		  case 0:
			/*
			 * Special case for no inputs: We mix into the current
			 * chain, if any!
			 */
			if(chainchannels)
				si->flags |= A2_PROCADD;
			break;
		  case A2_IO_DEFAULT:
			si->ninputs = si->unitdesc->mininputs;
			break;
		  case A2_IO_MATCHOUT:
			matchout = 1;
			break;
#ifdef DEBUG
		  case A2_IO_WIREOUT:
			a2c_Throw(c, A2_INTERNAL + 112);
#endif
		}
		if(si->ninputs)
		{
			/*
			 * If we have inputs, there must be a chain going, and
			 * it must have a matching channel count!
			 */
			if(!chainchannels)
				a2c_Throw(c, A2_NOINPUT);
			else if(si->ninputs != chainchannels)
				a2c_Throw(c, A2_CHAINMISMATCH);
		}

		/* Autowire outputs */
		switch(si->noutputs)
		{
		  case A2_IO_DEFAULT:
			/*
			 * Default output config! Last unit mixes to the voice
			 * output bus and grabs the channel count from there at
			 * instantiation. Other units use the default (minimum)
			 * channel count.
			 *    If there's no chain going, and no later units
			 * have inputs, we also send to the voice output.
			 */
			if(!si->next || (!chainchannels &&
					!downstream_inputs(si->next)))
				si->noutputs = A2_IO_WIREOUT;
			else
				si->noutputs = si->unitdesc->minoutputs;
			break;
		  case A2_IO_MATCHOUT:
			matchout = 1;
			break;
		}
		if(si->noutputs == A2_IO_WIREOUT)
		{
			chainchannels = 0;	/* Terminate chain! */
			si->flags |= A2_PROCADD; /* Mix instead of replace! */
		}
		else if(si->noutputs)
		{
			/* Only A2_IO_WIREOUT allowed for the final unit! */
			if(!si->next)
				a2c_Throw(c, A2_NOOUTPUT);

			/*
			 * If we already have a chain, but no inputs, we switch
			 * to adding mode in order to mix our output into the
			 * chain, instead of cutting it off by overwriting it!
			 */
			if(chainchannels && !si->ninputs)
				si->flags |= A2_PROCADD;

			/* We have a chain! */
			chainchannels = si->noutputs;
		}

		/* Any unit having inputs ==> use voice scratch buffers! */
		if(si->ninputs > p->buffers)
			p->buffers = si->ninputs;

		/* Make sure we have enough scratch buffers, if using them! */
		if(p->buffers && (si->noutputs > p->buffers))
			p->buffers = si->noutputs;

		DUMPSTRUCT(fprintf(stderr, " %d %d %s]", si->ninputs, si->noutputs,
				si->flags & A2_PROCADD ? "adding" : "replacing");)
	}
	if(matchout)
	{
		if(p->buffers)
			p->buffers = -p->buffers;
		else
			p->buffers = -1;
	}
	DUMPSTRUCT(
		fprintf(stderr, "\tbuffers: %d", p->buffers);
		if(p->vflags & A2_SUBINLINE)
			fprintf(stderr, "\tSUBINLINE");
		fprintf(stderr, "\n");
	)
}


static int a2c_AddFunction(A2_compiler *c)
{
	A2_program *p = c->coder->program;
	A2_function *fn = (A2_function *)realloc(p->funcs,
			(p->nfuncs + 1) * sizeof(A2_function));
	if(!fn)
		a2c_Throw(c, A2_OOMEMORY);
	p->funcs = fn;
	memset(p->funcs + p->nfuncs, 0, sizeof(A2_function));
	return p->nfuncs++;
}


static void a2c_ProgDef(A2_compiler *c, A2_symbol *s, int export)
{
	int i;
	A2_program *p;
	A2_scope sc;
	if(s->token != TK_NAME)
		a2c_Throw(c, A2_EXPNAME); /* TODO: Forward declarations. */
	if(c->coder || c->inhandler)
		a2c_Throw(c, A2_NOPROGHERE);	/* The top level only! */
	s->token = TK_PROGRAM;
	if(!(p = (A2_program *)calloc(1, sizeof(A2_program))))
		a2c_Throw(c, A2_OOMEMORY);
	for(i = 0; i < A2_MAXEPS; ++i)
		p->eps[i] = -1;
	if((s->v.i = rchm_New(&c->state->ss->hm, p, A2_TPROGRAM)) < 0)
	{
		free(p);
		a2c_Throw(c, -s->v.i);
	}
	if((i = a2ht_AddItem(&c->target->deps, s->v.i)) < 0)
		a2c_Throw(c, -i);
	if(export && !c->canexport)
		a2c_Throw(c, A2_CANTEXPORT);
	if(export || (c->exportall && c->canexport))
		s->flags |= A2_SF_EXPORTED;
	a2_PushSymbol(&c->symbols, s);
	DUMPCODE(fprintf(stderr, "program %s():\n", s->name);)
	a2c_PushCoder(c, p, 0);
	if(a2c_AddFunction(c) != 0)
		a2c_Throw(c, A2_INTERNAL + 131); /* Should be impossible! */
	a2c_BeginScope(c, &sc);
	a2c_ArgList(c, &c->coder->program->funcs[0]);
	a2c_SkipWhite(c, 1);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_StructDef(c);
	c->inhandler = c->nocode = 0;
	if(c->coder->program->structure)
		a2c_Code(c, OP_INITV, 0, 0);
	a2c_Body(c, '}');
	if(!c->nocode)
		a2c_Code(c, OP_END, 0, 0);
	a2c_EndScope(c, &sc);
	a2c_PopCoder(c);
	c->nocode = 1;
}


static void a2c_FuncDef(A2_compiler *c, A2_symbol *s)
{
	int f;
	A2_scope sc;
	if(s->token != TK_NAME)
		a2c_Throw(c, A2_EXPNAME); /* TODO: Forward declarations? */
	if(!c->coder || !c->coder->program || c->inhandler)
		a2c_Throw(c, A2_NOFUNCHERE);
	f = a2c_AddFunction(c);
	s->token = TK_FUNCTION;
	s->v.i = f;
	a2_PushSymbol(&c->symbols, s);
	DUMPCODE(fprintf(stderr, "function %s() (index %d):\n", s->name, f);)
	a2c_PushCoder(c, NULL, f);
	a2c_BeginScope(c, &sc);
	a2c_ArgList(c, &c->coder->program->funcs[f]);
	a2c_SkipWhite(c, 1);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_Body(c, '}');
	a2c_Code(c, OP_RETURN, 0, 0);
	a2c_EndScope(c, &sc);
	a2c_PopCoder(c);
}


static void a2c_MsgDef(A2_compiler *c, unsigned ep)
{
	int f;
	A2_scope sc;
	if(ep >= A2_MAXEPS)
		a2c_Throw(c, A2_BADENTRY);
	if(!c->coder || !c->coder->program || c->inhandler)
		a2c_Throw(c, A2_NOMSGHERE);
	DUMPCODE(fprintf(stderr, "message %d():\n", ep);)
	f = c->coder->program->eps[ep] = a2c_AddFunction(c);
	a2c_PushCoder(c, NULL, f);
	a2c_BeginScope(c, &sc);
	a2c_ArgList(c, &c->coder->program->funcs[f]);
	a2c_SkipWhite(c, 1);
	a2c_Expect(c, '{', A2_EXPBODY);
	c->inhandler = 1;
	c->nocode = 0;
	a2c_Body(c, '}');
	a2c_Code(c, OP_RETURN, 0, 0);
	c->inhandler = 0;
	a2c_EndScope(c, &sc);
	a2c_PopCoder(c);
	c->nocode = 1;
}


/* Wave and rendering options for 'wave' definitions */
typedef struct A2_wavedef {
	A2_symbol	*symbol;
	A2_wavetypes	type;
	unsigned	period;
	int		flags;
	unsigned	samplerate;
	unsigned	length;
	A2_handle	program;
	unsigned	argc;
	int		argv[A2_MAXARGS];
	double		duration;
	uint32_t	randseed;
	uint32_t	noiseseed;
} A2_wavedef;

static void a2c_wd_flagattr(A2_compiler *c, A2_wavedef *wd, unsigned flag)
{
	int set = 1;
	if(a2_IsValue(a2c_Lex(c, 0)))
		set = a2c_Num2Int(c, a2c_GetValue(c, &c->l[0]));
	else
		a2c_Unlex(c);
	if(set)
		wd->flags |= flag;
	else
		wd->flags &= ~flag;
}

static void a2c_wd_render(A2_compiler *c, A2_wavedef *wd,
		A2_tokens terminator)
{
	A2_property props[] = {
		{ A2_PRANDSEED,		wd->randseed	},
		{ A2_PNOISESEED,	wd->noiseseed	},
		{ 0, 0 }
	};
	int maxargc;
	if(wd->duration)
		wd->length = wd->duration * wd->samplerate;
	wd->program = a2c_GetHandle(c, &c->l[0]);
	maxargc = (a2_GetProgram(c->state, wd->program))->funcs[0].argc;
	wd->argc = a2c_ConstArguments(c, maxargc, wd->argv);
	DBG(
		fprintf(stderr, ".--------------------------------\n");
		fprintf(stderr, "| Rendering wave %s...\n", wd->symbol->name);
		fprintf(stderr, "|        type: %d\n", wd->type);
		fprintf(stderr, "|       flags: %x\n", wd->flags);
		fprintf(stderr, "|      period: %d\n", wd->period);
		fprintf(stderr, "|  samplerate: %d\n", wd->samplerate);
		fprintf(stderr, "|      length: %d\n", wd->length);
		fprintf(stderr, "|    randseed: %d\n", wd->randseed);
		fprintf(stderr, "|   noiseseed: %d\n", wd->noiseseed);
	)
	if((wd->symbol->v.i = a2_RenderWave(c->state,
			wd->type, wd->period, wd->flags,
			wd->samplerate, wd->length,
			wd->program, wd->argc, wd->argv, props)) < 0)
		a2c_Throw(c, -wd->symbol->v.i);
	if(wd->symbol->v.i < 0)
		a2c_Throw(c, -wd->symbol->v.i);
	DBG(	printf("|  DONE!\n");)

	/* We expect this to be the last statement in the wavedef! */
	while(a2c_Lex(c, 1) != terminator)
		if(c->l[0].token != TK_EOS)
			a2c_Throw(c, A2_EXPEOS);
	DBG(	fprintf(stderr, "'--------------------------------\n");)
}

static int a2c_WaveDefStatement(A2_compiler *c, A2_wavedef *wd,
		A2_tokens terminator)
{
	int tk = a2c_Lex(c, 0);
	switch(tk)
	{
	  case AT_PERIOD:
	  case AT_SAMPLERATE:
	  case AT_LENGTH:
	  case AT_DURATION:
	  case AT_RANDSEED:
	  case AT_NOISESEED:
	  {
		double v;
		a2c_SimplExp(c, -1);
		if(!a2_IsValue(c->l[0].token))
			a2c_Throw(c, A2_EXPCONSTANT);
		v = a2c_GetValue(c, &c->l[0]);
		switch(tk)
		{
		  case AT_PERIOD:
			wd->period = a2c_Num2Int(c, v);
			break;
		  case AT_SAMPLERATE:
			wd->samplerate = v;
			break;
		  case AT_LENGTH:
			wd->length = a2c_Num2Int(c, v);
			wd->duration = 0.0f;
			break;
		  case AT_DURATION:
			wd->duration = v;
			break;
		  case AT_RANDSEED:
			wd->randseed = v;
			break;
		  case AT_NOISESEED:
			wd->noiseseed = v;
			break;
		}
		break;
	  }
	  case AT_WAVETYPE:
		a2c_Expect(c, TK_WAVETYPE, A2_EXPWAVETYPE);
		wd->type = c->l[0].v.i;
		break;
	  case AT_FLAG:
		a2c_wd_flagattr(c, wd, c->l[0].v.i);
		break;
	  case TK_PROGRAM:
		a2c_wd_render(c, wd, terminator);
		return 0;
	  case TK_EOS:
		return 1;
	  default:
		if(c->l[0].token != terminator)
			a2c_Throw(c, A2_NEXPTOKEN);
		return 0;
	}
	if(a2c_Lex(c, 0) == TK_EOS)
		return 1;
	if(c->l[0].token != terminator)
		a2c_Throw(c, A2_EXPEOS);
	return 0;
}

static struct
{
	const char	*n;
	A2_tokens	tk;
	int		v;
} a2c_wdsyms [] = {
	{ "wavetype",	AT_WAVETYPE,	0		},
	{ "period",	AT_PERIOD,	0		},
	{ "samplerate",	AT_SAMPLERATE,	0		},
	{ "length",	AT_LENGTH,	0		},
	{ "duration",	AT_DURATION,	0		},
	{ "randseed",	AT_RANDSEED,	0		},
	{ "noiseseed",	AT_NOISESEED,	0		},
	{ "looped",	AT_FLAG,	A2_LOOPED	},
	{ "normalize",	AT_FLAG,	A2_NORMALIZE	},
	{ "xfade",	AT_FLAG,	A2_XFADE	},
	{ "revmix",	AT_FLAG,	A2_REVMIX	},

	{ "OFF",	TK_WAVETYPE,	A2_WOFF		},
	{ "NOISE",	TK_WAVETYPE,	A2_WNOISE	},
	{ "WAVE",	TK_WAVETYPE,	A2_WWAVE	},
	{ "MIPWAVE",	TK_WAVETYPE,	A2_WMIPWAVE	},

	{ "DEFAULT_RANDSEED",	TK_VALUE,	A2_DEFAULT_RANDSEED	},
	{ "DEFAULT_NOISESEED",	TK_VALUE,	A2_DEFAULT_NOISESEED	},

	{ NULL, 0, 0 }
};

static void a2c_WaveDef(A2_compiler *c)
{
	A2_scope sc;
	A2_wavedef wd;
	int i;
	int export = 1;
	memset(&wd, 0, sizeof(wd));
	wd.type = A2_WMIPWAVE;
	wd.samplerate = 44100;	/* FIXME: Parent state fs... or what...? */
	/*
	 * FIXME: If we were to actually save the *seed* somewhere, we should
	 * probably have substates inherit defaults from their parents.
	 */
	wd.randseed = A2_DEFAULT_RANDSEED;
	wd.noiseseed = A2_DEFAULT_NOISESEED;
	
	/* Local? */
	if(a2c_Lex(c, 0) == '.')
	{
		export = 0;
		a2c_Lex(c, 0);
	}

	/* Name of wave */
	if(c->l[0].token != TK_NAME)
		a2c_Throw(c, A2_EXPNAME);
	wd.symbol = a2c_GrabSymbol(c, &c->l[0]);
	wd.symbol->token = TK_WAVE;
	if(export || (c->exportall && c->canexport))
		wd.symbol->flags |= A2_SF_EXPORTED;
	a2_PushSymbol(&c->symbols, wd.symbol);

	a2c_SkipWhite(c, 1);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_BeginScope(c, &sc);

	/* Set up wave attribute symbols */
	for(i = 0; a2c_wdsyms[i].n; ++i)
	{
		A2_symbol *s;
		if(a2_FindSymbol(c->state, c->symbols, a2c_wdsyms[i].n))
			a2c_Throw(c, A2_SYMBOLDEF);
		if(!(s = a2_NewSymbol(a2c_wdsyms[i].n, a2c_wdsyms[i].tk)))
			a2c_Throw(c, A2_OOMEMORY);
		if(a2_IsValue(a2c_wdsyms[i].tk))
			s->v.f = a2c_wdsyms[i].v;
		else
			s->v.i = a2c_wdsyms[i].v;
		a2_PushSymbol(&c->symbols, s);
	}

	while(a2c_WaveDefStatement(c, &wd, '}'))
		;

	a2c_EndScope(c, &sc);
}


static void a2c_IfWhile(A2_compiler *c, A2_opcodes op, int loop)
{
	int fixpos, loopto = c->coder->pos;
	if(a2c_Lex(c, 0) == '(')
	{
		a2c_Unlex(c);
		a2c_SimplExp(c, -1);
		a2c_Branch(c, op, A2_UNDEFJUMP, &fixpos);
		a2c_SkipWhite(c, 1);
		a2c_Expect(c, '{', A2_EXPBODY);
	}
	else
	{
		a2c_Unlex(c);
		a2c_Expression(c, -1, '{');
		a2c_Branch(c, op, A2_UNDEFJUMP, &fixpos);
	}
	a2c_Body(c, '}');
	if(a2c_Lex(c, 1) == KW_ELSE)
	{
		int fixelse = c->coder->pos;
		if(loop)
			a2c_Throw(c, A2_NEXPELSE);
		a2c_Code(c, OP_JUMP, 0, A2_UNDEFJUMP);	/* To skip over 'else' body */
		if(fixpos >= 0)		/* False condition lands here! */
		{
			a2c_SetA2(c, fixpos, c->coder->pos);
			DUMPCODE(
				fprintf(stderr, "FIXUP: ");
				a2_DumpIns(c->coder->code, fixpos);
			)
		}
		a2c_SkipWhite(c, 1);
		a2c_Expect(c, '{', A2_EXPBODY);
		a2c_Body(c, '}');
		a2c_SetA2(c, fixelse, c->coder->pos);
		DUMPCODE(
			fprintf(stderr, "FIXUP: ");
			a2_DumpIns(c->coder->code, fixelse);
		)
		return;
	}
	else
		a2c_Unlex(c);
	if(loop)
		a2c_Code(c, OP_JUMP, 0, loopto);
	if(fixpos >= 0)
	{
		a2c_SetA2(c, fixpos, c->coder->pos);
		DUMPCODE(
			fprintf(stderr, "FIXUP: ");
			a2_DumpIns(c->coder->code, fixpos);
		)
	}
}


/*
 * "Repeat N times" block. Expects the count (value or register) to be in the
 * current lexer state!
 */
static void a2c_TimesL(A2_compiler *c)
{
	int loopto, r = a2c_AllocReg(c);
	a2c_CodeOpL(c, OP_LOAD, r, &c->l[0]);
	loopto = c->coder->pos;
	a2c_SkipWhite(c, 1);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_Body(c, '}');
	a2c_Code(c, OP_LOOP, r, loopto);
	a2c_FreeReg(c, r);
}


static void a2c_For(A2_compiler *c)
{
	int loopto = c->coder->pos;
	a2c_SkipWhite(c, 1);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_Body(c, '}');
	a2c_Code(c, OP_JUMP, 0, loopto);
}


static int a2c_Statement(A2_compiler *c, A2_tokens terminator)
{
	int r;
	switch(a2c_Lex(c, 0))
	{
	  case TK_VALUE:
		r = a2c_Num2Int(c, a2c_GetValue(c, &c->l[0]));
		switch(a2c_Lex(c, 0))
		{
		  case '(':
			a2c_MsgDef(c, r);
			return 1;
		  case '{':
			a2c_Unlex(c);
			a2c_TimesL(c);
			return 1;
		  case '<':
			a2c_Instruction(c, OP_SEND, r);
			break;
		  case ':':
			a2c_Instruction(c, OP_SPAWN, r);
			break;
		  default:
			a2c_Throw(c, A2_NEXPVALUE);
		}
		break;
	  case TK_REGISTER:
	  case TK_NAMESPACE:
		a2c_Unlex(c);
		r = a2c_Variable(c);
		switch(a2c_Lex(c, 0))
		{
		  case '{':
			a2c_Unlex(c);
			a2c_TimesL(c);
			return 1;
		  case '<':
			a2c_Instruction(c, OP_SENDR, r);
			break;
		  case ':':
			a2c_Instruction(c, OP_SPAWNV, r);
			break;
		  default:
			a2c_Unlex(c);
			a2c_SimplExp(c, r);
			a2c_CodeOpL(c, OP_LOAD, r, &c->l[0]);
			break;
		}
		break;
	  case '(':
	  {
		A2_tokens xtk;
		a2c_Unlex(c);
		a2c_SimplExp(c, -1);
		xtk = c->l[0].token;
		switch(xtk)
		{
		  case TK_VALUE:
			r = a2c_Num2Int(c, a2c_GetValue(c, &c->l[0]));
			switch(a2c_Lex(c, 0))
			{
			  case '{':
				a2c_Unlex(c);
				a2c_TimesL(c);
				return 1;
			  case '<':
				a2c_Instruction(c, OP_SEND, r);
				break;
			  case ':':
				a2c_Instruction(c, OP_SPAWN, r);
				break;
			  default:
				a2c_Throw(c, A2_NEXPVALUE);
			}
			break;
		  case TK_REGISTER:
		  case TK_TEMPREG:
			r = a2c_GetIndex(c, &c->l[0]);
			switch(a2c_Lex(c, 0))
			{
			  case '{':
				a2c_Unlex(c);
				a2c_TimesL(c);
				if(xtk == TK_TEMPREG)
					a2c_FreeReg(c, r);
				return 1;
			  case '<':
				a2c_Instruction(c, OP_SENDR, r);
				break;
			  case ':':
				a2c_Instruction(c, OP_SPAWNV, r);
				break;
			  default:
				a2c_Throw(c, A2_NEXPTOKEN);
			}
			if(xtk == TK_TEMPREG)
				a2c_FreeReg(c, r);
			break;
		  default:
			a2c_Throw(c, A2_NEXPTOKEN);
		}
		break;
	  }
	  case '.':		/* Label, def or local program */
		switch(a2c_Lex(c, 0))
		{
		  case TK_NAME:
		  case TK_FWDECL:
			if(a2c_Lex(c, 0) == '(')
				a2c_ProgDef(c, a2c_GrabSymbol(c, &c->l[1]), 0);
			else
			{
				A2_symbol *s;
				if(!c->coder)
					a2c_Throw(c, A2_NEXPLABEL);
				a2c_Unlex(c);
				s = a2c_GrabSymbol(c, &c->l[0]);
				s->token = TK_LABEL;
				s->v.i = c->coder->pos;
				a2_PushSymbol(&c->symbols, s);
				DUMPCODE(fprintf(stderr, "label .%s:\n", s->name);)
				if(c->l[0].token == TK_FWDECL)
					a2c_DoFixups(c, s);
			}
			return 1;
		  default:
			a2c_Throw(c, A2_BADLABEL);
		}
	  case TK_FWDECL:	/* TODO: Program forward declarations. */
		a2c_Throw(c, A2_SYMBOLDEF);
	  case TK_NAME:
		if(a2c_Lex(c, 0) != '(')
			a2c_Throw(c, A2_NEXPNAME);
		if(c->coder && c->coder->program)
			a2c_FuncDef(c, a2c_GrabSymbol(c, &c->l[1]));
		else
			a2c_ProgDef(c, a2c_GrabSymbol(c, &c->l[1]), 1);
		break;
	  case TK_LABEL:
		a2c_Throw(c, A2_SYMBOLDEF);	/* Already defined! */
	  case '!':
	  {
		A2_symbol *s;
		switch(a2c_Lex(c, 0))	/* For nicer error messages... */
		{
		  case TK_NAME:
			break;
		  case TK_REGISTER:
		  case TK_LABEL:
		  case TK_PROGRAM:
			a2c_Throw(c, A2_SYMBOLDEF);
		  default:
			a2c_Throw(c, A2_EXPNAME);
		}
		s = a2c_GrabSymbol(c, &c->l[0]);
		a2c_VarDecl(c, s);
		a2c_SimplExp(c, s->v.i);
		a2c_CodeOpL(c, OP_LOAD, s->v.i, &c->l[0]);
		break;
	  }
	  case ':':
		a2c_Instruction(c, OP_SPAWND, 0);
		break;
	  case '<':
		a2c_Instruction(c, OP_SENDS, 0);
		break;
	  case '+':
		a2c_Instruction(c, OP_ADD, 0);
		break;
	  case '-':
		a2c_Instruction(c, OP_SUBR, 0);
		break;
	  case '*':
		if(a2c_Lex(c, 0) == '<')
			a2c_Instruction(c, OP_SENDA, 0);
		else
		{
			a2c_Unlex(c);
			a2c_Instruction(c, OP_MUL, 0);
		}
		break;
	  case '/':
		a2c_Instruction(c, OP_DIVR, 0);
		break;
	  case '%':
		a2c_Instruction(c, OP_MOD, 0);
		break;
	  case TK_INSTRUCTION:
		a2c_Instruction(c, a2c_GetIndex(c, &c->l[0]), 0);
		break;
	  case TK_PROGRAM:
		a2c_Unlex(c);
		a2c_Instruction(c, OP_SPAWND, 0);
		break;
	  case TK_FUNCTION:
		a2c_Unlex(c);
		a2c_Instruction(c, OP_CALL, 0);
		break;
	  case KW_TEMPO:
		/* Calculate (1000 / (<tempo> / 60 * <tbp>)) */
		r = a2c_AllocReg(c);
		a2c_SimplExp(c, r);
		a2c_CodeOpL(c, OP_LOAD, r, &c->l[0]);
		a2c_Codef(c, OP_MUL, r, 1.0f / 60.0f);
		a2c_SimplExp(c, r);
		a2c_CodeOpL(c, OP_MUL, r, &c->l[0]);
		a2c_Codef(c, OP_LOAD, R_TICK, 1000.0f);
		a2c_Code(c, OP_DIVR, R_TICK, r);
		a2c_FreeReg(c, r);
		break;
	  case KW_IMPORT:
		a2c_Import(c, c->canexport);
		return 1;
	  case KW_DEF:
		a2c_Def(c, c->canexport);
		return 1;
	  case KW_WAVE:
		a2c_WaveDef(c);
		return 1;
	  case TK_IF:
		a2c_IfWhile(c, c->l[0].v.i, 0);
		return 1;
	  case TK_WHILE:
		a2c_IfWhile(c, c->l[0].v.i, 1);
		return 1;
	  case KW_FOR:
		a2c_For(c);
		return 1;
	  case '{':
		a2c_Body(c, '}');
		return 1;
	  case TK_EOS:
		return 1;
	  default:
		if(c->l[0].token != terminator)
			a2c_Throw(c, A2_NEXPTOKEN);
		return 0;
	}
	/* Finalizer for statements that expect a terminator */
	if(a2c_Lex(c, 0) == TK_EOS)
		return 1;
	if(c->l[0].token != terminator)
		a2c_Throw(c, A2_EXPEOS);
	return 0;
}


static void a2c_Statements(A2_compiler *c, A2_tokens terminator)
{
	while(a2c_Statement(c, terminator))
		;
}


static void a2c_Body(A2_compiler *c, A2_tokens terminator)
{
	A2_scope sc;
	a2c_BeginScope(c, &sc);
	a2c_Statements(c, terminator);
	a2c_EndScope(c, &sc);
}


static struct
{
	const char	*n;
	A2_tokens	tk;
	int		v;
} a2c_rootsyms [] = {
	/* Hardwired "root" bank 0 */
	{ "root",	TK_BANK,	0		},

	/* Hardwired control registers */
	{ "tick",	TK_REGISTER,	R_TICK		},
	{ "tr",		TK_REGISTER,	R_TRANSPOSE	},

	/* Instructions */
	{ "end",	TK_INSTRUCTION,	OP_END		},
	{ "return",	TK_INSTRUCTION,	OP_RETURN	},
	{ "jump",	TK_INSTRUCTION,	OP_JUMP		},
	{ "jz",		TK_INSTRUCTION,	OP_JZ		},
	{ "jnz",	TK_INSTRUCTION,	OP_JNZ		},
	{ "jg",		TK_INSTRUCTION,	OP_JG		},
	{ "jl",		TK_INSTRUCTION,	OP_JL		},
	{ "jge",	TK_INSTRUCTION,	OP_JGE		},
	{ "jle",	TK_INSTRUCTION,	OP_JLE		},
	{ "wake",	TK_INSTRUCTION,	OP_WAKE		},
	{ "force",	TK_INSTRUCTION,	OP_FORCE	},
	{ "wait",	TK_INSTRUCTION,	OP_WAIT		},
	{ "loop",	TK_INSTRUCTION,	OP_LOOP		},
	{ "kill",	TK_INSTRUCTION,	OP_KILL		},
	{ "d",		TK_INSTRUCTION,	OP_DELAY	},
	{ "td",		TK_INSTRUCTION,	OP_TDELAY	},
	{ "quant",	TK_INSTRUCTION,	OP_QUANT	},
	{ "rand",	TK_INSTRUCTION,	OP_RAND		},
	{ "p2d",	TK_INSTRUCTION,	OP_P2DR		},
	{ "neg",	TK_INSTRUCTION,	OP_NEGR		},
	{ "set",	TK_INSTRUCTION,	OP_SET		},
	{ "setall",	TK_INSTRUCTION,	OP_SETALL	},
	{ "sizeof",	TK_INSTRUCTION,	OP_SIZEOF	},
	{ "debug",	TK_INSTRUCTION,	OP_DEBUG	},

	/* Directives, macros, keywords... */
	{ "import",	KW_IMPORT,	0		},
	{ "def",	KW_DEF,		0		},
	{ "struct",	KW_STRUCT,	0		},
	{ "wire",	KW_WIRE,	0		},
	{ "tempo",	KW_TEMPO,	0		},
	{ "wave",	KW_WAVE,	0		},
	{ "if",		TK_IF,		OP_JZ		},
	{ "ifz",	TK_IF,		OP_JNZ		},
	{ "ifl",	TK_IF,		OP_JG		},
	{ "ifg",	TK_IF,		OP_JL		},
	{ "ifle",	TK_IF,		OP_JGE		},
	{ "ifge",	TK_IF,		OP_JLE		},
	{ "else",	KW_ELSE,	0		},
	{ "while",	TK_WHILE,	OP_JZ		},
	{ "wz",		TK_WHILE,	OP_JNZ		},
	{ "wl",		TK_WHILE,	OP_JGE		},
	{ "wg",		TK_WHILE,	OP_JLE		},
	{ "wle",	TK_WHILE,	OP_JG		},
	{ "wge",	TK_WHILE,	OP_JL		},
	{ "for",	KW_FOR,		0		},

	/* Operators */
	{ "and",	KW_AND,		0		},
	{ "or",		KW_OR,		0		},
	{ "xor",	KW_XOR,		0		},
	{ "not",	KW_NOT,		0		},

	{ NULL, 0, 0 }
};


A2_compiler *a2_OpenCompiler(A2_state *st, int flags)
{
	int i;
	A2_compiler *c = (A2_compiler *)calloc(1, sizeof(A2_compiler));
	if(!c)
		return NULL;
	flags |= st->config->flags & A2_INITFLAGS;
	c->lexbufpos = 0;
	c->lexbufsize = 64;
	if(!(c->lexbuf = (char *)malloc(c->lexbufsize)))
	{
		a2_CloseCompiler(c);
		return NULL;
	}
	c->state = st;
	for(i = 0; i < A2_CREGISTERS; ++i)
		c->regmap[i] = 1;
	c->exportall = (flags & A2_EXPORTALL) == A2_EXPORTALL;
	c->tabsize = st->ss->tabsize;
	for(i = 0; a2c_rootsyms[i].n; ++i)
	{
		A2_symbol *s = a2_NewSymbol(a2c_rootsyms[i].n,
				a2c_rootsyms[i].tk);
		if(!s)
		{
			a2_CloseCompiler(c);
			return NULL;
		}
		if(a2_IsValue(a2c_rootsyms[i].tk))
			s->v.f = a2c_rootsyms[i].v;
		else
			s->v.i = a2c_rootsyms[i].v;
		a2_PushSymbol(&c->symbols, s);
	}
	if(a2ht_AddItem(&c->imports, A2_ROOTBANK) < 0)
	{
		a2_CloseCompiler(c);
		return NULL;
	}
	return c;
}


void a2_CloseCompiler(A2_compiler *c)
{
	int i;
	for(i = 0; i < A2_LEXDEPTH; ++i)
		a2c_FreeToken(c, &c->l[i]);
	memset(c->l, 0, sizeof(c->l));
	while(c->symbols)
	{
		A2_symbol *s = c->symbols;
		c->symbols = s->next;
		a2_FreeSymbol(s);
	}
	while(c->coder)
		a2c_PopCoder(c);
	a2ht_Cleanup(&c->imports);
	free(c->lexbuf);
	if(c->path)
		free(c->path);
	free(c);
}


static void a2_Compile(A2_compiler *c, A2_scope *sc, const char *source)
{
	a2c_Try(c)
	{
		a2c_BeginScope(c, sc);
		c->canexport = 1;
		c->commawarned = 0;
		a2c_Statements(c, TK_EOF);
		a2c_EndScope(c, sc);
		return;
	}
	a2c_Except
	{
		int sline, scol, eline, ecol;
		a2c_CalculatePos(c, c->l[0].pos, &eline, &ecol);
		if(c->l[1].token)
			a2c_CalculatePos(c, c->l[1].pos, &sline, &scol);
		else
		{
			sline = eline;
			scol = ecol;
		}
		fprintf(stderr, "Audiality 2: %s ", a2_ErrorString(c->error));
		if((sline == eline) && (scol == ecol))
			fprintf(stderr, "at line %d, column %d", sline, scol);
		else if(sline == eline)
			fprintf(stderr, "at line %d, columns %d..%d",
					sline, scol, ecol);
		else
			fprintf(stderr, "between line %d, column %d and "
					"line %d, column %d",
					sline, scol, eline, ecol);
		fprintf(stderr, " in \"%s\"\n", source);
		if((sline == eline) && (scol == ecol))
			a2c_DumpLine(c, c->l[0].pos, 1, stderr);
		else if(sline == eline)
			/* FIXME: Underline range with markers! */
			a2c_DumpLine(c, c->l[1].pos, 1, stderr);
		else
		{
			a2c_DumpLine(c, c->l[1].pos, 1, stderr);
			/* FIXME: This could span more than two lines... */
			a2c_DumpLine(c, c->l[0].pos, 1, stderr);
		}
	}
	/* Try to avoid dangling wires and stuff... */
	a2c_Try(c)
	{
		while(c->coder)
			a2c_PopCoder(c);
	}
	a2c_Except
	{
		fprintf(stderr, "Audiality 2: Emergency finalization 1: %s\n",
				a2_ErrorString(c->error));
	}
	a2c_Try(c)
	{
		a2c_CleanScope(c, sc);
	}
	a2c_Except
	{
		fprintf(stderr, "Audiality 2: Emergency finalization 2: %s\n",
				a2_ErrorString(c->error));
	}
}


A2_errors a2_CompileString(A2_compiler *c, A2_handle bank, const char *code,
		const char *source)
{
	A2_scope sc;
	c->target = a2_GetBank(c->state, bank);
	if(!c->target)
		return A2_INVALIDHANDLE;
	c->source = code;
	c->l[0].pos = 0;
	c->inhandler = 0;
	c->nocode = 1;
	a2_Compile(c, &sc, source);
	return c->error;
}


A2_errors a2_CompileFile(A2_compiler *c, A2_handle bank, const char *fn)
{
	int res;
	char *code;
	FILE *f;
	size_t fsize;
	const char *slashpos;
	if(!(f = fopen(fn, "rb")))
		return A2_OPEN;
	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	code = malloc(fsize + 1);
	if(!code)
	{
		fclose(f);
		return A2_OOMEMORY;
	}
	code[fsize] = 0;
	if(fread(code, 1, fsize, f) != fsize)
	{
		fclose(f);
		free(code);
		return A2_READ;
	}
	fclose(f);
	if((slashpos = strrchr(fn, '/')))
	{
		/* Grab directory path of 'fn' for local imports */
		int bufsize = slashpos - fn;
		if(!(c->path = malloc(bufsize + 1)))
			return A2_OOMEMORY;
		memcpy(c->path, fn, bufsize);
		c->path[bufsize] = 0;
	}
	res = a2_CompileString(c, bank, code, fn);
	free(code);
#ifdef A2_OUT_A2B
	{
		A2_bank *b = a2_GetBank(c->state, bank);
		if(!b)
			return A2_INVALIDHANDLE;
		A2_symbol *s = b->exports;
		f = fopen("out.a2b", "wb");
		fwrite(&b->end, 1, 3, f);
		fwrite(bank->code, b->end, sizeof(unsigned), f);
		for( ; s; s = s->next)
		{
			fwrite(s->name, 1, strlen(s->name) + 1, f);
			fwrite(&s->value, 1, 3, f);
		}
		fclose(f);
	}
#endif
	return res;
}
