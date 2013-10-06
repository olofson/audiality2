/*
 * compiler.c - Audiality 2 Script (A2S) compiler
 *
 * Copyright 2010-2012 David Olofson <david@olofson.net>
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
#include "compiler.h"
#include "units/inline.h"


/*---------------------------------------------------------
	Symbols
---------------------------------------------------------*/

static A2_symbol *a2_NewSymbol(const char *name, A2_tokens token, int value)
{
	A2_symbol *s = (A2_symbol *)calloc(1, sizeof(A2_symbol));
	if(!s)
		return NULL;
	s->name = strdup(name);
	s->token = token;
	s->value = value;
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
	printf("a2_PushSymbol(\"%s\" tk:%d v:%d(%f) i:%d)\n",
			s->name, s->token, s->value, s->value / 65536.0f, s->index);
#endif
	s->next = *stack;
	*stack = s;
}

static A2_symbol *a2_FindSymbol(A2_state *st, A2_symbol *s, const char *name)
{
	for( ; s; s = s->next)
		if(!strcmp(name, s->name))
			return s;
	return NULL;
}

static A2_symbol *a2c_Grab(A2_compiler *c, A2_symbol *s)
{
	if(c->l.sym == s)
		c->l.sym = NULL;
	if(c->pl.sym == s)
		c->pl.sym = NULL;
	return s;
}


/*---------------------------------------------------------
	Handles and objects
---------------------------------------------------------*/

/* Add dependency on 'h' to current bank, if there isn't one already */
static void a2c_AddDependency(A2_compiler *c, A2_handle h)
{
	RCHM_errors res;
	if(a2ht_FindItem(&c->target->deps, h) >= 0)
		return;		/* Already in there! */
	if((res = a2ht_AddItem(&c->target->deps, h)) < 0)
		a2c_Throw(c, -res);
}


/*---------------------------------------------------------
	Coder
---------------------------------------------------------*/

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
	A2_function *fn;
	A2_coder *cdr = c->coder;
	if(!cdr)
		a2c_Throw(c, A2_INTERNAL + 130);	/* No coder!? */
	fn = cdr->program->funcs + cdr->func;
	fn->code = (unsigned *)realloc(cdr->code,
			(cdr->pos + 1) * sizeof(unsigned));
	if(!fn->code)
		a2c_Throw(c, A2_OOMEMORY);
	fn->code[cdr->pos] = OP_END << 26;
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
	A2_coder *cdr = c->coder;
	if(c->nocode)
		a2c_Throw(c, A2_NOCODE);
	if(cdr->pos + 2 >= cdr->size)
	{
		int i, ns = cdr->size;
		unsigned *nc;
		if(ns)
			while(cdr->pos + 2 >= ns)
				ns = ns * 3 / 2;
		else
			ns = 64;
		nc = (unsigned *)realloc(cdr->code, ns * sizeof(unsigned));
		if(!nc)
			a2c_Throw(c, A2_OOMEMORY);
		cdr->code = nc;
		cdr->size = ns;
		for(i = cdr->pos; i < cdr->size; ++i)
			cdr->code[i] = OP_END << 26;
	}
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
		if(!a2_GetProgram(c->state, arg))
			a2c_Throw(c, A2_BADPROGRAM);
		break;
	  case OP_SEND:
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
	  case OP_QUANTR:
	  case OP_SPAWNR:
	  case OP_SPAWNDR:
#if 0
	  case OP_RAMPR:
	  case OP_DETACHR:
#endif
		if(arg > A2_REGISTERS)
			a2c_Throw(c, A2_BADREG2);
		break;
	  case OP_WAIT:
/*FIXME: Should probably add all instructions that don't use 'arg' here... */
		break;
	  default:
		arg = a2_i2f(arg);
		break;
	}
	if(arg > 0x1fffff)
		a2c_Throw(c, A2_BADIMMARG);
	cdr->code[cdr->pos++] = (op << 26) | (reg << 21) | arg;
	DUMPCODE(a2_DumpIns(cdr->code, cdr->pos - 1);)
}


static A2_errors a2c_DoFixups(A2_compiler *c, A2_symbol *s)
{
	A2_coder *cdr = c->coder;
	if(s->value > 0x1fffff)
		return A2_BADIMMARG;
	while(s->fixups)
	{
		A2_fixup *fx = s->fixups;
		s->fixups = fx->next;
		cdr->code[cdr->pos] |= s->value;
		DUMPCODE(printf("FIXUP: "); a2_DumpIns(cdr->code, cdr->pos);)
		free(fx);
	}
	return A2_OK;
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
	int ch = c->source[c->l.pos];
	if(!ch)
		return -1;
	++c->l.pos;
	return ch;
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
 * Read decimal value, return as signed 16:16 fixed point.
 * Returns A2_OK if everything is normal, otherwise an appropriate error code;
 *	A2_OVERFLOW if the parsed value is too large to fit in a 16:16 fixp
 *	A2_UNDERFLOW if a non-zero parsed value is truncated to zero
 *	A2_BADVALUE if a proper number could not be parsed at all
 */
static A2_errors a2_GetNum(A2_compiler *c, int ch, int *v)
{
	int sign = 1;
	double val = 0.0f;
	unsigned xp = 0;
	int valid = 0;	/* At least one figure, or this is not a number! */
	if(ch == '-')
	{
		sign = -1;
		ch = a2_GetChar(c);
	}
	for(;; ch = a2_GetChar(c))
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
			{
				--c->l.pos;
				*v = 0;
				return A2_BADVALUE;
			}
			xp = 1;
		}
		else
		{
			if(!valid)
			{
				--c->l.pos;
				*v = 0;
				return A2_BADVALUE;
			}
			val *= sign;
			if(xp)
				val /= xp;
			if(ch == 'n')
				val /= 12.0f;
			else if(ch == 'f')
				val = a2_F2P(val);
			else
				--c->l.pos;
			if(val >= 32767.0f)
				*v = 0x7fffffff;
			else if(val <= -32768.0f)
				*v = 0x80000000;
			else
			{
				*v = val * 65536.0f + 0.5f;
				return val && !*v ? A2_UNDERFLOW : A2_OK;
			}
			return A2_OVERFLOW;
		}
}

/* Parse a double quoted string with some basic C style control codes */
static int a2c_LexString(A2_compiler *c)
{
	A2_errors res;
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
#if 0
			  case '0':
			  case '1':
			  case '2':
			  case '3':
				ch = get_num(es, 8, 2);
				if(ch < 0)
					eel_cerror(es, "Illegal octal number!");
				break;
				ch += 64 * (ch - '0');
				break;
#endif
			  case 'a':
				ch = '\a';
				break;
			  case 'b':
				ch = '\b';
				break;
			  case 'c':
				ch = '\0';
				break;
			  case 'd':
				if((res = a2_GetNum(c, ch, &ch)))
					a2c_Throw(c, res);
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
#if 0
			  case 'x':
				ch = get_num(es, 16, 2);
				if(ch < 0)
					eel_cerror(es, "Illegal hex number!");
				break;
#endif
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
	c->l.string = a2c_ndup(c->lexbuf, c->lexbufpos);
	if(!c->l.string)
		a2c_Throw(c, A2_OOMEMORY);
#if 0
	c->l.token = TK_STRINGLIT;
#else
	c->l.token = TK_STRING;
	c->l.val = a2_NewString(c->state, c->l.string);
	if(c->l.val < 0)
		a2c_Throw(c, -c->l.val);
	a2c_AddDependency(c, c->l.val);
#endif
	return c->l.token;
}

/* NOTE:
 *	This returns names as new symbols. Use a2_Grab() to keep one of those -
 *	or it will be deleted as parsing continues!
 *	   Also note that TK_NAME symbols MUST NOT be used in any symbol tables,
 *	as the parser may delete them whenever they end up in *l.sym!
 */
static int a2c_Lex(A2_compiler *c)
{
	char *name;
	int nstart, ch, prevch;
	A2_handle h;
	if(c->pl.sym && (c->pl.sym->token == TK_NAME))
		a2_FreeSymbol(c->pl.sym);	/* Free if not grabbed! */
	free(c->pl.string);
	c->pl = c->l;
	c->l.sym = NULL;
	c->l.string = NULL;
	c->lexbufpos = 0;

	/* Handle whitespace, comments and control characters */
	while(1)
	{
		switch((ch = a2_GetChar(c)))
		{
		  case -1:
			return (c->l.token = TK_EOF);
		  case ',':
		  case ';':
		  case '\n':
			c->l.val = ch;
			return (c->l.token = TK_EOS);
		  case ' ':
		  case '\t':
		  case '\r':
			continue;
		  case '/':
			switch(a2_GetChar(c))
			{
			  case '/':
				while((ch = a2_GetChar(c)) != '\n')
					if(ch == -1)
						return (c->l.token = TK_EOF);
				--c->l.pos;
				continue;
			  case '*':
				for(prevch = 0; (ch = a2_GetChar(c)) != -1;
						prevch = ch)
					if((prevch == '*') && (ch == '/'))
						break;
				continue;
			}
			--c->l.pos;
			break;
		  case '"':
			return a2c_LexString(c);
		}
		break;
	}

	/* Numeric literals */
	if(((ch >= '0') && (ch <= '9')) || (ch == '-') || (ch == '.'))
	{
		A2_errors res = a2_GetNum(c, ch, &c->l.val);
		if(res == A2_OK)
			return (c->l.token = TK_VALUE);
		else if(res != A2_BADVALUE)
			a2c_Throw(c, res);
	}

	/* Check for valid identefiers */
	nstart = c->l.pos - 1;
	while(((ch >= 'a') && (ch <= 'z')) ||
			((ch >= 'A') && (ch <= 'Z')) ||
			((ch >= '0') && (ch <= '9')) ||
			(ch == '_'))
		ch = a2_GetChar(c);
	if(nstart == c->l.pos - 1)
		return (c->l.token = ch);	/* Nope? Return as token! */
	--c->l.pos;
	name = a2c_ndup(c->source + nstart, c->l.pos - nstart);
	DUMPLSTRINGS(fprintf(stderr, " [\"%s\":  ", name);)

	/* Try the symbol stack... */
	if((c->l.sym = a2_FindSymbol(c->state, c->symbols, name)))
	{
		DUMPLSTRINGS(fprintf(stderr, "symbol %p] ", c->l.sym);)
		c->l.token = c->l.sym->token;
		c->l.val = c->l.sym->value;
		free(name);
		return c->l.token;	/* Symbol! */
	}

	/* Try imports... */
	if((h = a2_find_import(c, name)) >= 0)
	{
		c->l.token = 0;
		switch(a2_TypeOf(c->state, h))
		{
		  /* Valid types */
		  case A2_TBANK:	c->l.token = TK_BANK; break;
		  case A2_TWAVE:	c->l.token = TK_WAVE; break;
		  case A2_TUNIT:	c->l.token = TK_UNIT; break;
		  case A2_TPROGRAM:	c->l.token = TK_PROGRAM; break;
		  case A2_TSTRING:	c->l.token = TK_STRING; break;
		  /* Warning eliminator - still warns if we miss a type! */
		  case A2_TDETACHED:
		  case A2_TVOICE:
			break;
		}
		if(c->l.token)
		{
			/*
			 * No deps! We rely on objects found this way to be
			 * managed by our owner, or owned by an imported bank!
			 */
			c->l.val = h;
			c->l.string = name;
			DUMPLSTRINGS(fprintf(stderr, "token %d] ", c->l.token);)
			return c->l.token;
		}
	}

	/* No symbol, no import! Return it as a new name. */
	if(!(c->l.sym = a2_NewSymbol(name, TK_NAME, 0)))
	{
		DUMPLSTRINGS(fprintf(stderr, "COULD NOT CREATE SYMBOL!] ");)
		free(name);
		a2c_Throw(c, A2_OOMEMORY);
	}
	c->l.token = c->l.sym->token;
	c->l.val = c->l.sym->value;
	DUMPLSTRINGS(fprintf(stderr, "name] ");)
	free(name);
	return c->l.token;
}

#if 0
static int a2c_Lex(A2_compiler *c)
{
	int r = a2c_Lex2(c);
fprintf(stderr, "[%d: %d '%c']\n", c->l.pos, r, r);
	return r;
}
#endif

static int a2c_LexNamespace(A2_compiler *c, A2_symbol *namespace)
{
	int tk;
	A2_symbol *ssave = c->symbols;
	c->symbols = namespace;
	tk = a2c_Lex(c);
	c->symbols = ssave;
	return tk;
}


static void a2c_Unlex(A2_compiler *c)
{
	DUMPLSTRINGS(fprintf(stderr, "[unlex] ");)
	if(c->l.sym && (c->l.sym->token == TK_NAME))
		a2_FreeSymbol(c->l.sym);
	free(c->l.string);
	c->l = c->pl;
	memset(&c->pl, 0, sizeof(A2_lexstate));
}

static void a2c_SkipLF(A2_compiler *c)
{
	while(a2c_Lex(c) == TK_EOS)
		if(c->l.val != '\n')
			break;
	a2c_Unlex(c);
}


/*
 * Returns the value of the specified lexer state in a format for suitable for
 * use in VM registers and function/spawn/send arguments.
 */
static int a2_GetVMValue(A2_lexstate *l)
{
	switch(l->token)
	{
	  /*
	   * Handles are integers, so we need to normalize them to integer
	   * values in the VM fixed point format to avoid corruption.
	   */
	  case TK_BANK:
	  case TK_WAVE:
	  case TK_UNIT:
	  case TK_PROGRAM:
	  case TK_STRING:
		return l->val << 16;
	  default:
		return l->val;
	}
}


/*---------------------------------------------------------
	Scope management
---------------------------------------------------------*/

typedef struct A2_scope
{
	A2_symbol	*symbols;
	unsigned	regtop;
	int		canexport;
} A2_scope;

static void a2c_BeginScope(A2_compiler *c, A2_scope *sc)
{
	sc->symbols = c->symbols;
	sc->regtop = c->regtop;
	sc->canexport = c->canexport;
	c->canexport = 0;	/* Only top level scope can export normally! */
}

static void a2c_EndScope(A2_compiler *c, A2_scope *sc)
{
	int res = A2_OK;
	A2_nametab *x = &c->target->exports;
	c->regtop = sc->regtop;

	free(c->l.string);
	free(c->pl.string);
	c->l.string = c->pl.string = NULL;
	if(c->l.sym && (c->l.sym->token == TK_NAME))
		a2_FreeSymbol(c->l.sym);
	if(c->pl.sym && (c->pl.sym->token == TK_NAME))
		a2_FreeSymbol(c->pl.sym);
	c->l.sym = c->pl.sym = NULL;

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
			h = s->value;
			DBG(fprintf(stderr, "h: %d\t", h);)
			DBG(fprintf(stderr, "t: %s\t", a2_TypeName(c->state,
					a2_TypeOf(c->state, h)));)
			break;
		  default:
			h = -1;
			DBG(fprintf(stderr, "(unsupported)\t");)
			break;
		}
		DBG(if(s->exported)
			fprintf(stderr, "EXPORTED\n"); else fprintf(stderr, "\n");)
		if(s->exported)
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
#if 0
		else if(h >= 0)
			a2ht_AddItem(d, h);
#endif
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
	c->regtop = sc->regtop;
	free(c->l.string);
	free(c->pl.string);
	c->l.string = c->pl.string = NULL;
	if(c->l.sym && (c->l.sym->token == TK_NAME))
		a2_FreeSymbol(c->l.sym);
	if(c->pl.sym && (c->pl.sym->token == TK_NAME))
		a2_FreeSymbol(c->pl.sym);
	c->l.sym = c->pl.sym = NULL;
	while(c->symbols != sc->symbols)
	{
		A2_symbol *s = c->symbols;
		c->symbols = s->next;
		a2_FreeSymbol(s);
	}
	c->canexport = sc->canexport;
}


/*---------------------------------------------------------
	VM register allocation
---------------------------------------------------------*/

static unsigned a2c_AllocReg(A2_compiler *c)
{
	if(c->regtop >= A2_REGISTERS)
		a2c_Throw(c, A2_OUTOFREGS);
	return c->regtop++;
}

static void a2c_FreeReg(A2_compiler *c, unsigned r)
{
#ifdef DEBUG
	if(r != c->regtop - 1)
	{
		fprintf(stderr, "Audiality 2 INTERNAL ERROR: Tried to free VM"
				"registers out of order!\n");
		a2c_Throw(c, A2_INTERNAL + 100);
	}
#endif
	--c->regtop;
}


/*---------------------------------------------------------
	Parser
---------------------------------------------------------*/

static void a2c_Expect(A2_compiler *c, A2_tokens tk, A2_errors err)
{
	if(a2c_Lex(c) != tk)
		a2c_Throw(c, err);
}


static void a2c_Branch(A2_compiler *c, A2_opcodes op, int xtk, int xval,
		unsigned to, int *fixpos)
{
	int r;
	if(a2_IsValue(xtk))
	{
		/* FIXME:
		 *	These are no conditionals! They should be evaluated
		 *	at compile time instead.
		 */
		r = a2c_AllocReg(c);
		a2c_Code(c, OP_LOAD, r, xval);
		if(fixpos)
			*fixpos = c->coder->pos;
		a2c_Code(c, op, r, to);
		a2c_FreeReg(c, r);
	}
	else if(a2_IsRegister(xtk))
	{
		if(fixpos)
			*fixpos = c->coder->pos;
		a2c_Code(c, op, xval, to);
		if(xtk == TK_TEMPREG)
			a2c_FreeReg(c, xval);
	}
	else
		a2c_Throw(c, A2_INTERNAL + 101);
}


static void a2c_VarDecl(A2_compiler *c, A2_symbol *s)
{
	s->token = TK_REGISTER;
	s->value = a2c_AllocReg(c);
	a2_PushSymbol(&c->symbols, s);
}


static void a2c_CodeOpR(A2_compiler *c, A2_opcodes op, int to, unsigned r)
{
	switch(op)
	{
	  case OP_ADD:
	  case OP_MUL:
	  case OP_MOD:
	  case OP_QUANT:
	  case OP_RAND:
	  case OP_LOAD:
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
		a2c_Code(c, op, to, r);
		break;
	  default:
		a2c_Throw(c, A2_BADOPCODE);
	}
}

static void a2c_CodeOpV(A2_compiler *c, A2_opcodes op, int to, int v)
{
	int tmpr = to;
	switch(op)
	{
	  case OP_MOD:
		if(!v)
			a2c_Throw(c, A2_DIVBYZERO);
	  case OP_ADD:
	  case OP_MUL:
	  case OP_QUANT:
	  case OP_RAND:
	  case OP_LOAD:
	  case OP_DELAY:	/* ('to' is not used by these last four) */
	  case OP_TDELAY:
	  case OP_DEBUG:
		a2c_Code(c, op, to, v);
		break;
	  case OP_SUBR:
		a2c_Code(c, OP_ADD, to, -v);
		break;
	  case OP_DIVR:
		if(!v)
			a2c_Throw(c, A2_DIVBYZERO);
		a2c_Code(c, OP_MUL, to, 0x100000000LL / v);
		break;
	  default:
		if((op != OP_RAND) && (op != OP_P2DR) && (op != OP_NEGR))
			tmpr = a2c_AllocReg(c);
		a2c_Code(c, OP_LOAD, tmpr, v);
		a2c_CodeOpR(c, op, to, tmpr);
		if(tmpr != to)
			a2c_FreeReg(c, tmpr);
		break;
	}
}

/*
 * Issue code '<op> <to> <from>', where <from> is derived from the parser state
 * after a a2c_SimplExp() call. Any temporary register will be freed.
 */
static void a2c_CodeOpL(A2_compiler *c, A2_opcodes op, int to)
{
	switch(c->l.token)
	{
	  case TK_VALUE:
	  case TK_WAVE:
	  case TK_PROGRAM:
		a2c_CodeOpV(c, op, to, a2_GetVMValue(&c->l));
		break;
	  case TK_TEMPREG:
	  case TK_REGISTER:
		a2c_CodeOpR(c, op, to, c->l.val);
		if(c->l.token == TK_TEMPREG)
			a2c_FreeReg(c, c->l.val);
		break;
	  default:
		a2c_Throw(c, A2_INTERNAL + 102);	/* We should never get here! */
	}
}


static void a2c_SimplExp(A2_compiler *c, int r);

/* Expression, terminated with ')' */
static void a2c_Expression(A2_compiler *c, int to)
{
	int op;
	a2c_SkipLF(c);
	a2c_SimplExp(c, to);
	if((c->l.token == TK_PROGRAM) || (c->l.token == TK_WAVE))
		a2c_Throw(c, A2_NEXPHANDLE); /* No arithmetics on these! */
	a2c_CodeOpL(c, OP_LOAD, to);
	while(1)
	{
		a2c_SkipLF(c);
		switch(a2c_Lex(c))
		{
		  case ')':
			return;	/* Done !*/
		  case TK_INSTRUCTION:	op = c->l.val;	break;
		  case '+':		op = OP_ADD;	break;
		  case '-':		op = OP_SUBR;	break;
		  case '*':		op = OP_MUL;	break;
		  case '/':		op = OP_DIVR;	break;
		  case '%':		op = OP_MOD;	break;
		  default:		a2c_Throw(c, A2_EXPOP);
		}
		a2c_SkipLF(c);
		a2c_SimplExp(c, -1);
		if((c->l.token == TK_PROGRAM) || (c->l.token == TK_WAVE))
			a2c_Throw(c, A2_NEXPHANDLE);
		a2c_CodeOpL(c, op, to);
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
	switch(a2c_Lex(c))
	{
	  case TK_REGISTER:
		return c->l.val;
	  case TK_NAMESPACE:
	  {
		A2_symbol *ns = c->l.sym->symbols;
		a2c_Expect(c, '.', A2_NEXPTOKEN);
		if(a2c_LexNamespace(c, ns) != TK_REGISTER)
			a2c_Throw(c, A2_EXPVARIABLE);
		return c->l.val;
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
 */
static void a2c_SimplExp(A2_compiler *c, int r)
{
	int op, tmpr = r;
	switch(a2c_Lex(c))
	{
	  case TK_VALUE:
	  case TK_WAVE:
	  case TK_PROGRAM:
	  case TK_STRING:
		return;		/* Done! Just return as is - no code! */
#if 0
	  case TK_STRINGLIT:
		c->l.token = TK_STRING;
		c->l.val = a2_NewString(c->state, c->l.string);
		if(c->l.val < 0)
			a2c_Throw(c, -c->l.val);
		a2c_AddDependency(c, c->l.val);
		return;
#endif
	  case '(':
		if(r < 0)
			tmpr = a2c_AllocReg(c);
		a2c_Expression(c, tmpr);
		break;
	  case TK_INSTRUCTION:	/* Unary operator */
		op = c->l.val;
		if((op != OP_P2DR) && (op != OP_RAND) && (op != OP_NEGR))
			a2c_Throw(c, A2_NOTUNARY);
		if(r < 0)
			tmpr = a2c_AllocReg(c);
		a2c_SimplExp(c, tmpr);
		a2c_CodeOpL(c, op, tmpr);
		break;
	  default:
		a2c_Unlex(c);
		a2c_Variable(c);
		return;
	}
	c->l.token = r < 0 ? TK_TEMPREG : TK_REGISTER; /* Temp or allocated? */
	c->l.val = tmpr;
}


static void a2c_Arguments(A2_compiler *c, int maxargc)
{
	int argc;
	for(argc = 0; argc <= maxargc; ++argc)
	{
		if((a2c_Lex(c) == '}') || (c->l.token == TK_EOS))
		{
			a2c_Unlex(c);
			return;		/* Done! */
		}
		a2c_Unlex(c);
		a2c_SimplExp(c, -1);
		if(a2_IsValue(c->l.token))
			a2c_Code(c, OP_PUSH, 0, a2_GetVMValue(&c->l));
		else if(a2_IsRegister(c->l.token))
		{
			a2c_Code(c, OP_PUSHR, c->l.val, 0);
			if(c->l.token == TK_TEMPREG)
				a2c_FreeReg(c, c->l.val);
		}
		else
			/* Shouldn't happen: a2c_SimplExp() should fail! */
			a2c_Throw(c, A2_INTERNAL + 112);
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
		if((a2c_Lex(c) != TK_LABEL) && (c->l.token != TK_FWDECL))
			a2c_Throw(c, A2_EXPLABEL);
		a2c_Code(c, op, 0, c->l.val);
		break;
	  case OP_LOOP:
		r = a2c_Variable(c);
		a2c_Expect(c, TK_LABEL, A2_EXPLABEL);
		a2c_Code(c, op, r, c->l.val);
		break;
	  case OP_JZ:
	  case OP_JNZ:
	  case OP_JG:
	  case OP_JL:
	  case OP_JGE:
	  case OP_JLE:
		a2c_SimplExp(c, -1);
		a2c_Expect(c, TK_LABEL, A2_EXPLABEL);
		a2c_Branch(c, op, c->pl.token, c->pl.val, c->l.val, NULL);
		break;
	  case OP_SPAWN:
	  case OP_SPAWND:
		switch(a2c_Lex(c))
		{
		  case TK_REGISTER:
			++op;
			p = c->l.val;
			i = A2_MAXARGS;	/* Can't check these compile time... */
			break;
		  case TK_PROGRAM:
			p = c->l.val;	/* Program handle */
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
		p = c->l.val;	/* Function entry point */
		if(p >= c->coder->program->nfuncs)
			a2c_Throw(c, A2_BADENTRY); /* Invalid entry point! */
		i = c->coder->program->funcs[p].argc;
		a2c_Arguments(c, i);
		a2c_Code(c, op, r, p);
		break;
	  case OP_WAIT:
		if(c->inhandler)
			a2c_Throw(c, A2_NORUN);
		a2c_Expect(c, TK_VALUE, A2_EXPVALUE);
		a2c_Code(c, op, c->l.val >> 16, 0);
		break;
	  case OP_SEND:
	  case OP_SENDS:
	  case OP_SENDA:
		a2c_Expect(c, TK_VALUE, A2_EXPVALUE);
		p = c->l.val >> 16;	/* Message handler entry point */
		if(!p)
			a2c_Throw(c, A2_BADENTRY); /* 0 is not for messages! */
		a2c_Arguments(c, A2_MAXARGS);
		a2c_Code(c, op, r, p);
		break;
	  case OP_KILL:
/*TODO: Expression!!! */
		if(a2c_Lex(c) == '*')
			a2c_Code(c, OP_KILLA, 0, 0);
		else
		{
			if(c->l.token != TK_VALUE)
				a2c_Throw(c, A2_EXPVALUE);
			a2c_Code(c, OP_KILL, c->l.val >> 16, 0);
		}
		break;
	  case OP_SET:
		a2c_Code(c, OP_SET, a2c_Variable(c), 0);
		break;
	  case OP_DELAY:
	  case OP_TDELAY:
		if(c->inhandler)
			a2c_Throw(c, A2_NOTIMING);
		/* Fall through! */
	  case OP_DEBUG:
		a2c_SimplExp(c, -1);
		a2c_CodeOpL(c, op, 0);
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
		if(a2c_Lex(c) == '!')
		{
			A2_symbol *s;
			if((op != OP_RAND) && (op != OP_P2DR) && (op != OP_NEGR))
				a2c_Throw(c, A2_BADVARDECL);
			a2c_Expect(c, TK_NAME, A2_EXPNAME);
			s = a2c_Grab(c, c->l.sym);
			a2c_VarDecl(c, s);
			r = s->value;
		}
		else
		{
			a2c_Unlex(c);
			r = a2c_Variable(c);
		}
		a2c_SimplExp(c, (op == OP_RAND) || (op == OP_P2DR) ||
				(op == OP_NEGR)? r : -1);
		a2c_CodeOpL(c, op, r);
		break;
	  default:
		a2c_Throw(c, A2_BADOPCODE);
	}
}


static void a2c_Def(A2_compiler *c, int export)
{
	A2_symbol *s;
	a2c_Expect(c, TK_NAME, A2_EXPNAME);
	s = a2c_Grab(c, c->l.sym);
	/*
	 * A bit ugly; we just ignore the 'export' argument if we're in a
	 * scope from where symbols cannot be exported...
	 */
	if(c->canexport)
		s->exported = export || c->exportall;
	switch(a2c_Lex(c))
	{
	  case TK_VALUE:
		s->token = TK_VALUE;
		s->value = c->l.val;
		break;
	  case TK_LABEL:
	  case TK_REGISTER:
		s->exported = 0;	/* In case we have 'exportall'... */
		if(export)
			a2c_Throw(c, A2_NOEXPORT);
		/* Fall through! */
	  case TK_WAVE:
	  case TK_PROGRAM:
	  case TK_STRING:
		s->token = c->l.token;
		s->value = c->l.val;
		break;
	  default:
		a2c_Throw(c, A2_BADVALUE);
	}
	a2_PushSymbol(&c->symbols, s);
}


static void a2c_Body(A2_compiler *c, A2_tokens terminator);


static void a2c_ArgList(A2_compiler *c, A2_function *fn)
{
	uint8_t *argc = &fn->argc;
	fn->argv = c->regtop;
	for(*argc = 0; a2c_SkipLF(c), a2c_Lex(c) != ')'; ++*argc)
	{
		if(*argc > A2_MAXARGS)
			a2c_Throw(c, A2_MANYARGS);
		if(c->l.token != TK_NAME)
			a2c_Throw(c, A2_EXPNAME);
		a2c_VarDecl(c, a2c_Grab(c, c->l.sym));
		if(a2c_Lex(c) == '=')
		{
			if(!a2_IsValue(a2c_Lex(c)))
				a2c_Throw(c, A2_EXPVALUE);
			fn->argdefs[*argc] = a2_GetVMValue(&c->l);
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
	DUMPSTRUCT(printf("  %s %d %d [", ud->name, inputs, outputs);)

	/* Allocate control registers and add them to the namespace! */
	if(!namespace)
		namespace = &c->symbols;
	for(i = 0; ud->registers[i].name; ++i)
	{
		A2_symbol *s;
		if(a2_FindSymbol(c->state, *namespace, ud->registers[i].name))
			a2c_Throw(c, A2_SYMBOLDEF);
		if(!(s = a2_NewSymbol(ud->registers[i].name, TK_REGISTER,
				a2c_AllocReg(c))))
			a2c_Throw(c, A2_OOMEMORY);
		a2_PushSymbol(namespace, s);
		DUMPSTRUCT(printf(" %s:R%d", s->name, s->value);)
	}
	DUMPSTRUCT(printf(" ]\n");)
	return ind;
}


/* Create and push a namespace and return the head of its local symbol stack */
static A2_symbol **a2c_CreateNamespace(A2_compiler *c, const char *name,
		A2_tokens kind)
{
	A2_symbol *s = a2_NewSymbol(name, TK_NAMESPACE, kind);
	if(!s)
		a2c_Throw(c, A2_OOMEMORY);
	a2_PushSymbol(&c->symbols, s);
	return &s->symbols;
}


static int a2c_IOSpec(A2_compiler *c, int min, int max, int outputs)
{
	switch(a2c_Lex(c))
	{
	  case TK_VALUE:
	  {
		int val;
		if(c->l.val & 0xffff)
			a2c_Throw(c, A2_EXPINTEGER);
		val = c->l.val >> 16;
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
	const A2_unitdesc *ud = a2_GetUnit(c->state, c->l.val);
	if(!ud)
		a2c_Throw(c, A2_INTERNAL + 107); /* Object missing!? */
	switch(a2c_Lex(c))
	{
	  case TK_NAME:
		/* Named unit! Put the control registers in a namespace. */
		namespace = a2c_CreateNamespace(c, c->l.sym->name, TK_UNIT);
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
	switch(a2c_Lex(c))
	{
	  case TK_UNIT:
		a2c_UnitSpec(c);
		break;
	  case TK_WIRE:
		a2c_WireSpec(c);
		break;
	  case TK_EOS:
		return 1;
	  default:
		if(c->l.token != terminator)
			a2c_Throw(c, A2_NEXPTOKEN);
		return 0;
	}
	if(a2c_Lex(c) == TK_EOS)
		return 1;
	if(c->l.token != terminator)
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
	if(a2c_Lex(c) != TK_STRUCT)
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
	DUMPSTRUCT(printf("\tbuffers: %d", p->buffers);)
	DUMPSTRUCT(if(p->vflags & A2_SUBINLINE) printf("\tSUBINLINE");)
	DUMPSTRUCT(printf("\n");)
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
	A2_errors res;
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
	if((s->value = rchm_New(&c->state->ss->hm, p, A2_TPROGRAM)) < 0)
	{
		free(p);
		a2c_Throw(c, -s->value);
	}
	if((res = a2ht_AddItem(&c->target->deps, s->value)) < 0)
		a2c_Throw(c, -res);
	if(export && !c->canexport)
		a2c_Throw(c, A2_CANTEXPORT);
	s->exported = export || (c->exportall && c->canexport);
	a2_PushSymbol(&c->symbols, s);
	DUMPCODE(printf("program %s():\n", s->name);)
	a2c_PushCoder(c, p, 0);
	if(a2c_AddFunction(c) != 0)
		a2c_Throw(c, A2_INTERNAL + 131); /* Should be impossible! */
	a2c_BeginScope(c, &sc);
	a2c_ArgList(c, &c->coder->program->funcs[0]);
	a2c_SkipLF(c);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_SkipLF(c);
	a2c_StructDef(c);
	c->inhandler = c->nocode = 0;
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
	s->value = f;
	a2_PushSymbol(&c->symbols, s);
	DUMPCODE(printf("function %s() (index %d):\n", s->name, f);)
	a2c_PushCoder(c, NULL, f);
	a2c_BeginScope(c, &sc);
	a2c_ArgList(c, &c->coder->program->funcs[f]);
	a2c_SkipLF(c);
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
	DUMPCODE(printf("message %d():\n", ep);)
	f = c->coder->program->eps[ep] = a2c_AddFunction(c);
	a2c_PushCoder(c, NULL, f);
	a2c_BeginScope(c, &sc);
	a2c_ArgList(c, &c->coder->program->funcs[f]);
	a2c_SkipLF(c);
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


static void a2c_IfWhile(A2_compiler *c, A2_opcodes op, int loop)
{
	int fixpos, loopto = c->coder->pos;
	a2c_SimplExp(c, -1);
	a2c_Branch(c, op, c->l.token, c->l.val, A2_UNDEFJUMP, &fixpos);
	a2c_SkipLF(c);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_Body(c, '}');
	if(a2c_Lex(c) == TK_ELSE)
	{
		int fixelse = c->coder->pos;
		if(loop)
			a2c_Throw(c, A2_NEXPELSE);
		a2c_Code(c, OP_JUMP, 0, A2_UNDEFJUMP);	/* To skip over 'else' body */
		if(fixpos >= 0)		/* False condition lands here! */
		{
			c->coder->code[fixpos] |= c->coder->pos;
			DUMPCODE(printf("FIXUP: "); a2_DumpIns(c->coder->code, fixpos);)
		}
		a2c_SkipLF(c);
		a2c_Expect(c, '{', A2_EXPBODY);
		a2c_Body(c, '}');
		c->coder->code[fixelse] |= c->coder->pos;
		DUMPCODE(printf("FIXUP: "); a2_DumpIns(c->coder->code, fixelse);)
		return;
	}
	else
		a2c_Unlex(c);
	if(loop)
		a2c_Code(c, OP_JUMP, 0, loopto);
	if(fixpos >= 0)
	{
		c->coder->code[fixpos] |= c->coder->pos;
		DUMPCODE(printf("FIXUP: "); a2_DumpIns(c->coder->code, fixpos);)
	}
}


/*
 * "Repeat N times" block. Expects the count (value or register) to be in the
 * current lexer state!
 */
static void a2c_TimesL(A2_compiler *c)
{
	int loopto, r = a2c_AllocReg(c);
	a2c_CodeOpL(c, OP_LOAD, r);
	loopto = c->coder->pos;
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_Body(c, '}');
	a2c_Code(c, OP_LOOP, r, loopto);
	a2c_FreeReg(c, r);
}


static void a2c_For(A2_compiler *c)
{
	int loopto = c->coder->pos;
	a2c_SkipLF(c);
	a2c_Expect(c, '{', A2_EXPBODY);
	a2c_Body(c, '}');
	a2c_Code(c, OP_JUMP, 0, loopto);
}


static int a2c_Statement(A2_compiler *c, A2_tokens terminator)
{
	int r;
	switch(a2c_Lex(c))
	{
	  case TK_VALUE:
/*TODO: Do this for expressions as well, for '<', ':' and '{' */
		r = c->l.val >> 16;
		switch(a2c_Lex(c))
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
		if(a2c_Lex(c) == '{')
		{
			a2c_Unlex(c);
			a2c_TimesL(c);
		}
		else
		{
			a2c_Unlex(c);
			a2c_SimplExp(c, r);
			a2c_CodeOpL(c, OP_LOAD, r);
		}
		break;
	  case '.':		/* Label, def or local program */
		switch(a2c_Lex(c))
		{
		  case TK_DEF:
			a2c_Def(c, 0);
			return 1;
		  case TK_NAME:
		  case TK_FWDECL:
			if(a2c_Lex(c) == '(')
				a2c_ProgDef(c, a2c_Grab(c, c->pl.sym), 0);
			else
			{
				A2_symbol *s;
				a2c_Unlex(c);
				s = a2c_Grab(c, c->l.sym);
				s->token = TK_LABEL;
				s->value = c->coder->pos;
				a2_PushSymbol(&c->symbols, s);
				DUMPCODE(printf("label .%s:\n", s->name);)
				if(c->l.token == TK_FWDECL)
					a2c_DoFixups(c, s);
			}
			return 1;
		  default:
			a2c_Throw(c, A2_BADLABEL);
		}
	  case TK_FWDECL:	/* TODO: Program forward declarations. */
		a2c_Throw(c, A2_SYMBOLDEF);
	  case TK_NAME:
		if(a2c_Lex(c) != '(')
			a2c_Throw(c, A2_NEXPNAME);
		if(c->coder && c->coder->program)
			a2c_FuncDef(c, a2c_Grab(c, c->pl.sym));
		else
			a2c_ProgDef(c, a2c_Grab(c, c->pl.sym), 1);
		break;
	  case TK_LABEL:
		a2c_Throw(c, A2_SYMBOLDEF);	/* Already defined! */
	  case '!':
	  {
		A2_symbol *s;
		switch(a2c_Lex(c))	/* For nicer error messages... */
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
		s = a2c_Grab(c, c->l.sym);
		a2c_VarDecl(c, s);
		a2c_SimplExp(c, s->value);
		a2c_CodeOpL(c, OP_LOAD, s->value);
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
		if(a2c_Lex(c) == '<')
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
		a2c_Instruction(c, c->l.val, 0);
		break;
	  case TK_PROGRAM:
		a2c_Unlex(c);
		a2c_Instruction(c, OP_SPAWND, 0);
		break;
	  case TK_RUN:
		fprintf(stderr, "Audiality 2 WARNING: 'run' is a hack with "
				"inaccurate timing! Please stop using it.\n");
		r = A2_REGISTERS - 1;
		a2c_Instruction(c, OP_SPAWN, r);
		a2c_Code(c, OP_WAIT, r, 0);
		break;
	  case TK_FUNCTION:
		a2c_Unlex(c);
		a2c_Instruction(c, OP_CALL, 0);
		break;
	  case TK_TEMPO:
		/* Calculate (1000 / (<tempo> / 60 * <tbp>)) */
		r = a2c_AllocReg(c);
		a2c_SimplExp(c, r);
		a2c_CodeOpL(c, OP_LOAD, r);
		a2c_Code(c, OP_MUL, r, 65536 / 60);
		a2c_SimplExp(c, r);
		a2c_CodeOpL(c, OP_MUL, r);
		a2c_Code(c, OP_LOAD, R_TICK, 1000 << 16);
		a2c_Code(c, OP_DIVR, R_TICK, r);
		a2c_FreeReg(c, r);
		break;
	  case TK_DEF:
		a2c_Def(c, c->canexport);
		return 1;
	  case TK_IF:
		a2c_IfWhile(c, c->l.val, 0);
		return 1;
	  case TK_WHILE:
		a2c_IfWhile(c, c->l.val, 1);
		return 1;
	  case TK_FOR:
		a2c_For(c);
		return 1;
	  case '{':
		a2c_Body(c, '}');
		return 1;
	  case TK_EOS:
		return 1;
	  default:
		if(c->l.token != terminator)
			a2c_Throw(c, A2_NEXPTOKEN);
		return 0;
	}
	/* Finalizer for statements that expect a terminator */
	if(a2c_Lex(c) == TK_EOS)
		return 1;
	if(c->l.token != terminator)
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
} initsyms [] = {
	/* Hardwired "root" bank 0 */
	{"root",	TK_BANK,	0},

	/* Hardwired control registers */
	{"tick",	TK_REGISTER,	R_TICK},
	{"tr",		TK_REGISTER,	R_TRANSPOSE},

	/* Instructions */
	{"end",		TK_INSTRUCTION,	OP_END},
	{"return",	TK_INSTRUCTION,	OP_RETURN},
	{"jump",	TK_INSTRUCTION,	OP_JUMP},
	{"jz",		TK_INSTRUCTION,	OP_JZ},
	{"jnz",		TK_INSTRUCTION,	OP_JNZ},
	{"jg",		TK_INSTRUCTION,	OP_JG},
	{"jl",		TK_INSTRUCTION,	OP_JL},
	{"jge",		TK_INSTRUCTION,	OP_JGE},
	{"jle",		TK_INSTRUCTION,	OP_JLE},
	{"wake",	TK_INSTRUCTION,	OP_WAKE},
	{"force",	TK_INSTRUCTION,	OP_FORCE},
	{"wait",	TK_INSTRUCTION,	OP_WAIT},
	{"loop",	TK_INSTRUCTION,	OP_LOOP},
	{"kill",	TK_INSTRUCTION,	OP_KILL},
	{"d",		TK_INSTRUCTION,	OP_DELAY},
	{"td",		TK_INSTRUCTION,	OP_TDELAY},
	{"quant",	TK_INSTRUCTION,	OP_QUANT},
	{"rand",	TK_INSTRUCTION,	OP_RAND},
	{"p2d",		TK_INSTRUCTION,	OP_P2DR},
	{"neg",		TK_INSTRUCTION,	OP_NEGR},
	{"set",		TK_INSTRUCTION,	OP_SET},
	{"debug",	TK_INSTRUCTION,	OP_DEBUG},

	/* Directives, macros, keywords... */
	{"def",		TK_DEF,		0},
	{"struct",	TK_STRUCT,	0},
	{"wire",	TK_WIRE,	0},
	{"tempo",	TK_TEMPO,	0},
	{"if",		TK_IF,		OP_JZ},
	{"ifz",		TK_IF,		OP_JNZ},
	{"ifl",		TK_IF,		OP_JG},
	{"ifg",		TK_IF,		OP_JL},
	{"ifle",	TK_IF,		OP_JGE},
	{"ifge",	TK_IF,		OP_JLE},
	{"else",	TK_ELSE,	0},
	{"while",	TK_WHILE,	OP_JZ},
	{"wz",		TK_WHILE,	OP_JNZ},
	{"wl",		TK_WHILE,	OP_JGE},
	{"wg",		TK_WHILE,	OP_JLE},
	{"wle",		TK_WHILE,	OP_JG},
	{"wge",		TK_WHILE,	OP_JL},
	{"for",		TK_FOR,		0},
	{"run",		TK_RUN,		0},

	{NULL, 0, 0}
};


A2_errors a2_OpenCompiler(A2_state *st, int flags)
{
	int i;
	A2_compiler *c = (A2_compiler *)calloc(1, sizeof(A2_compiler));
	if(!c)
		return A2_OOMEMORY;
	c->lexbufpos = 0;
	c->lexbufsize = 64;
	if(!(c->lexbuf = (char *)malloc(c->lexbufsize)))
		return A2_OOMEMORY;
	c->state = st;
	st->ss->c = c;
	c->regtop = A2_CREGISTERS;
	c->exportall = (flags & A2_EXPORTALL) == A2_EXPORTALL;
	for(i = 0; initsyms[i].n; ++i)
	{
		A2_symbol *s = a2_NewSymbol(initsyms[i].n, initsyms[i].tk,
				initsyms[i].v);
		if(!s)
			return A2_OOMEMORY;
		a2_PushSymbol(&c->symbols, s);
	}
	if(a2ht_AddItem(&c->imports, A2_ROOTBANK) < 0)
		return A2_OOMEMORY;
	return A2_OK;
}


void a2_CloseCompiler(A2_compiler *c)
{
	free(c->l.string);
	if(c->l.sym && (c->l.sym->token == TK_NAME))
		a2_FreeSymbol(c->l.sym);
	free(c->pl.string);
	if(c->pl.sym && (c->pl.sym->token == TK_NAME))
		a2_FreeSymbol(c->pl.sym);
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
	free(c);
}


static void a2_Compile(A2_compiler *c, A2_scope *sc, const char *source)
{
	a2c_Try(c)
	{
		a2c_BeginScope(c, sc);
		c->canexport = 1;
		a2c_Statements(c, TK_EOF);
		a2c_EndScope(c, sc);
		return;
	}
	a2c_Except
	{
		int line, col, i;
		for(line = col = 1, i = 0; i < c->l.pos ; ++i)
			if(c->source[i] == '\n')
				++line, col = 1;
			else
				col += (c->source[i] == '\t') ? 8 : 1;
		fprintf(stderr, "Audiality 2: %s at line %d, column %d in "
				"\"%s\"\n",
				a2_ErrorString(c->error), line, col, source);
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
	c->l.pos = 0;
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
	f = fopen(fn, "rb");
	if(!f)
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


/*---------------------------------------------------------
	Debug tools
---------------------------------------------------------*/

#define	A2_DEFERR(x, y)	y,
static const char *a2_errnames[100] = {
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
		fputs(a2_regnames[r], stdout);
	else
		printf("R%d", r);
}

void a2_DumpIns(unsigned *code, unsigned pc)
{
	unsigned ins = code[pc];
	A2_opcodes op = ins >> 26;
	int reg = (ins >> 21) & 0x1f;
	int arg = ins & 0x1fffff;
	printf("%d:\t%-8.8s", pc, a2_insnames[op]);
	switch(op)
	{
	  /* No arguments */
	  case OP_END:
	  case OP_RETURN:
	  case OP_SLEEP:
	  case OP_KILLA:
	  case OP_INITV:
	  case A2_OPCODES:	/* (Warning eliminator) */
		break;
	  /* integer */
	  case OP_JUMP:
	  case OP_WAKE:
	  case OP_FORCE:
	  case OP_SENDA:
	  case OP_SENDS:
	  case OP_CALL:
		printf("%d", arg);
		break;
	  /* f20 */
	  case OP_DELAY:
	  case OP_TDELAY:
	  case OP_PUSH:
	  case OP_DEBUG:
		printf("%f", a2_f2i(arg) / 65536.0f);
		break;
	  /* register */
	  case OP_DELAYR:
	  case OP_TDELAYR:
	  case OP_PUSHR:
	  case OP_SET:
	  case OP_SPAWNDR:
	  case OP_DEBUGR:
		a2_PrintRegName(reg);
		break;
	  /* register f20 */
	  case OP_LOAD:
	  case OP_ADD:
	  case OP_MUL:
	  case OP_MOD:
	  case OP_QUANT:
	  case OP_RAND:
		a2_PrintRegName(reg);
		printf(" %f", a2_f2i(arg) / 65536.0f);
		break;
	  /* register integer */
	  case OP_LOOP:
	  case OP_JZ:
	  case OP_JNZ:
	  case OP_JG:
	  case OP_JL:
	  case OP_JGE:
	  case OP_JLE:
		a2_PrintRegName(reg);
		printf(" %d", arg);
		break;
	  /* index */
	  case OP_KILL:
	  case OP_WAIT:
		printf("%d", reg);
		break;
	  /* index integer */
	  case OP_SPAWN:
	  case OP_SPAWND:
	  case OP_SEND:
		printf("%d %d", reg, arg);
		break;
	  /* register register */
	  case OP_LOADR:
	  case OP_ADDR:
	  case OP_SUBR:
	  case OP_MULR:
	  case OP_DIVR:
	  case OP_MODR:
	  case OP_QUANTR:
	  case OP_RANDR:
	  case OP_SPAWNR:
	  case OP_P2DR:
	  case OP_NEGR:
		a2_PrintRegName(reg);
		printf(" ");
		a2_PrintRegName(arg);
		break;
	}
	printf("\n");
}
