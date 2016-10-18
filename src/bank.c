/*
 * bank.c - Audiality 2 banks and symbols
 *
 * Copyright 2010-2014, 2016 David Olofson <david@olofson.net>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "internals.h"
#include "compiler.h"


/*---------------------------------------------------------
	Object/handle management
---------------------------------------------------------*/

static RCHM_errors a2_BankDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	int i;
	A2_state *st = ((A2_typeinfo *)ti)->state;
	A2_bank *b = (A2_bank *)hi->d.data;
	if(hi->userbits & A2_LOCKED)
		return RCHM_REFUSE;
	a2nt_Cleanup(&b->exports);
	a2nt_Cleanup(&b->private);
	for(i = 0; i < b->deps.nitems; ++i)
		a2_Release(st, b->deps.items[i]);
	a2ht_Cleanup(&b->deps);
	free(b->name);
	free(b);
	return 0;
}

static RCHM_errors a2_ProgramDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	int i;
	A2_state *st = ((A2_typeinfo *)ti)->state;
	A2_program *p = (A2_program *)hi->d.data;
	if(hi->userbits & A2_LOCKED)
		return RCHM_REFUSE;
	a2_KillVoicesUsingProgram(st, h);
	while(p->units)
	{
		A2_structitem *pp = p->units;
		p->units = pp->next;
		free(pp);
	}
	while(p->wires)
	{
		A2_structitem *pp = p->wires;
		p->wires = pp->next;
		free(pp);
	}
	for(i = 0; i < p->nfuncs; ++i)
		free(p->funcs[i].code);
	free(p->funcs);
	free(p);
	return RCHM_OK;
}

static RCHM_errors a2_StringDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	A2_string *s = (A2_string *)hi->d.data;
	if(hi->userbits & A2_LOCKED)
		return RCHM_REFUSE;
	free(s->buffer);
	free(s);
	return RCHM_OK;
}

A2_errors a2_RegisterBankTypes(A2_state *st)
{
	A2_errors res = a2_RegisterType(st, A2_TBANK, "bank",
			a2_BankDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TPROGRAM, "program",
				a2_ProgramDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TSTRING, "string",
				a2_StringDestructor, NULL);
	if(res)
		return res;
	return A2_OK;
}


/*---------------------------------------------------------
	Bank management
---------------------------------------------------------*/

A2_handle a2_NewBank(A2_state *st, const char *name, int flags)
{
	A2_handle h;
	char buf[64];
	A2_bank *bank = (A2_bank *)calloc(1, sizeof(A2_bank));
	if(!bank)
		return -A2_OOMEMORY;
	h = rchm_NewEx(&st->ss->hm, bank, A2_TBANK, flags, 1);
	if(h < 0)
		return -h;
	if(!name)
	{
		snprintf(buf, sizeof(buf) - 1, "bank%p", bank);
		name = buf;
	}
	bank->name = strdup(name);
	if(!bank->name)
	{
		a2_Release(st, h);
		return -A2_OOMEMORY;
	}
	DBG(printf("created bank \"%s\", handle %d\n", name, h);)
	return h;
}


/*---------------------------------------------------------
	Loading and compiling scripts
---------------------------------------------------------*/

A2_handle a2_LoadString(A2_state *st, const char *code, const char *name)
{
	int res;
	A2_handle h;
	A2_compiler *c;
	if(!(c = a2_OpenCompiler(st, 0)))
		return -A2_OOMEMORY;
	if((h = a2_NewBank(st, name, A2_APIOWNED)) < 0)
	{
		a2_CloseCompiler(c);
		return h;
	}
	if((res = a2_CompileString(c, h, code, name)) != A2_OK)
	{
		a2_CloseCompiler(c);
		a2_Release(st, h);
		return -res;
	}
	a2_CloseCompiler(c);
	return h;
}


A2_handle a2_Load(A2_state *st, const char *fn, unsigned flags)
{
	int res;
	A2_handle h;
	A2_compiler *c;
	char *fnx = NULL;
	/* If there's no extension, add ".a2s" */
	if(!strchr(fn, '.'))
	{
		fnx = malloc(strlen(fn) + 4 + 1);
		strcpy(fnx, fn);
		strcpy(fnx + strlen(fn), ".a2s");
		fn = fnx;
	}
#if 0
	/* TODO: */
	flags |= st->config->flags & A2_INITFLAGS;
	if(!(flags & A2_NOSHARED))
	{
		/* Try to find previously loaded bank first! */

	}
#endif
	if(!(c = a2_OpenCompiler(st, 0)))
	{
		free(fnx);
		return -A2_OOMEMORY;
	}
	if((h = a2_NewBank(st, fn, A2_APIOWNED)) < 0)
	{
		free(fnx);
		a2_CloseCompiler(c);
		return h;
	}
	if((res = a2_CompileFile(c, h, fn)) != A2_OK)
	{
		free(fnx);
		a2_CloseCompiler(c);
		a2_Release(st, h);
		return -res;
	}
	free(fnx);
	a2_CloseCompiler(c);
	return h;
}


/*---------------------------------------------------------
	Strings
---------------------------------------------------------*/

A2_handle a2_NewString(A2_state *st, const char *string)
{
	A2_handle h;
	A2_string *s;
	if(!(s = (A2_string *)calloc(1, sizeof(A2_string))))
		return -A2_OOMEMORY;
	if(!(s->buffer = strdup(string)))
	{
		free(s);
		return -A2_OOMEMORY;
	}
	s->length = strlen(string);
	if((h = rchm_New(&st->ss->hm, s, A2_TSTRING)) < 0)
		return h;
	DBG(printf("created string \"%s\", handle %d\n", string, h);)
	return h;
}


/*---------------------------------------------------------
	Objects and exports
---------------------------------------------------------*/

A2_errors a2_Assign(A2_state *st, A2_handle owner, A2_handle handle)
{
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, owner)))
		return A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return A2_DEADHANDLE;
	switch(hi->typecode)
	{
	  case A2_TBANK:
	  {
		int ind;
		A2_bank *b = (A2_bank *)hi->d.data;
		if(a2ht_FindItem(&b->deps, handle) >= 0)
			return A2_ISASSIGNED;
		if((ind = a2ht_AddItem(&b->deps, handle)) < 0)
			return -ind;
		if((hi = rchm_Get(&st->ss->hm, handle)))
			hi->userbits &= ~A2_APIOWNED;
		return A2_OK;
	  }
	  case A2_TWAVE:
	  case A2_TUNIT:
	  case A2_TPROGRAM:
	  case A2_TSTRING:
	  case A2_TVOICE:
		return A2_WRONGTYPE;
	}
	return A2_BADTYPE;
}


A2_errors a2_Export(A2_state *st, A2_handle owner, A2_handle handle,
		const char *name)
{
	A2_errors res;
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, owner)))
		return A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return A2_DEADHANDLE;
	if(!name)
		if(!(name = a2_Name(st, handle)))
			return A2_NONAME;
	switch(hi->typecode)
	{
	  case A2_TBANK:
	  {
		A2_bank *b = (A2_bank *)hi->d.data;
		if((res = a2nt_AddItem(&b->exports, name, handle) < 0))
			return -res;
		/* Ensure that we have this object listed as a dependency! */
		return a2_Assign(st, owner, handle);
	  }
	  case A2_TWAVE:
	  case A2_TUNIT:
	  case A2_TPROGRAM:
	  case A2_TSTRING:
	  case A2_TVOICE:
		return A2_WRONGTYPE;
	}
	return A2_BADTYPE;
}


A2_handle a2_Get(A2_state *st, A2_handle node, const char *path)
{
	A2_handle h;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, node);
	if(!hi)
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	switch(hi->typecode)
	{
	  case A2_TBANK:
	  {
		A2_bank *b = (A2_bank *)hi->d.data;
		if(!b)
			return -A2_INVALIDHANDLE;
		if((h = a2nt_FindItem(&b->exports, path)) >= 0)
			break;
		if((h = a2nt_FindItem(&b->private, path)) >= 0)
			break;
		return -A2_NOTFOUND;
	  }
	  default:
		return -A2_WRONGTYPE;
	}
	if((path = strchr(path, '.')) && path[1])
		return a2_Get(st, h, path + 1);	/* Recurse into containers! */
	return h;
}


A2_handle a2_GetExport(A2_state *st, A2_handle node, int i)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, node);
	if(!hi)
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	switch(hi->typecode)
	{
	  case A2_TBANK:
	  {
		A2_bank *b = (A2_bank *)hi->d.data;
		if(i >= 0)
		{
			if(i >= b->exports.nitems)
				return -A2_INDEXRANGE;
			return b->exports.items[i].handle;
		}
		else
		{
			i = -1 - i;
			if(i >= b->private.nitems)
				return -A2_INDEXRANGE;
			return b->private.items[i].handle;
		}
	  }
	  default:
		return -A2_WRONGTYPE;
	}
}


const char *a2_GetExportName(A2_state *st, A2_handle node, int i)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, node);
	if(!hi)
		return NULL;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return NULL;
	switch(hi->typecode)
	{
	  case A2_TBANK:
	  {
		A2_bank *b = (A2_bank *)hi->d.data;
		if(i >= 0)
		{
			if(i >= b->exports.nitems)
				return NULL;
			return b->exports.items[i].name;
		}
		else
		{
			i = -1 - i;
			if(i >= b->private.nitems)
				return NULL;
			return b->private.items[i].name;
		}
	  }
	  default:
		return NULL;
	}
}
