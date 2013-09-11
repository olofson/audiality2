/*----------------------------------------------------------------------------.
        rchm.c - Reference Counting Handle Manager 0.1                        |
 .----------------------------------------------------------------------------'
 | Copyright (C) 2012 David Olofson <david@olofson.net>
 |
 | This software is provided 'as-is', without any express or implied warranty.
 | In no event will the authors be held liable for any damages arising from the
 | use of this software.
 |
 | Permission is granted to anyone to use this software for any purpose,
 | including commercial applications, and to alter it and redistribute it
 | freely, subject to the following restrictions:
 |
 | 1. The origin of this software must not be misrepresented; you must not
 |    claim that you wrote the original software. If you use this software
 |    in a product, an acknowledgment in the product documentation would be
 |    appreciated but is not required.
 | 2. Altered source versions must be plainly marked as such, and must not be
 |    misrepresented as being the original software.
 | 3. This notice may not be removed or altered from any source distribution.
 '---------------------------------------------------------------------------*/

#include "rchm.h"
#include <stdlib.h>
#include <string.h>


RCHM_errors rchm_RegisterType(RCHM_manager *m, RCHM_typecode tc,
		const char *name, RCHM_destructor_cb destroy, void *userdata)
{
	if(!m->types || (tc >= m->ntypes))
	{
		int nsize = tc + 8;
		RCHM_typeinfo *nti = (RCHM_typeinfo *)realloc(m->types,
				nsize * sizeof(RCHM_typeinfo));
		if(!nti)
			return RCHM_OOMEMORY;
		memset(nti + m->ntypes, 0,
				(nsize - m->ntypes) * sizeof(RCHM_typeinfo));
		m->types = nti;
		m->ntypes = nsize;
	}
	m->types[tc].destructor = destroy;
	m->types[tc].userdata = userdata;
	free(m->types[tc].name);
	if(name)
		m->types[tc].name = strdup(name);
	else
		m->types[tc].name = NULL;
	return RCHM_OK;
}


const char *rchm_TypeName(RCHM_manager *m, RCHM_typecode tc)
{
	if(tc >= m->ntypes)
		return NULL;
	return m->types[tc].name;
}


void *rchm_TypeUserdata(RCHM_manager *m, RCHM_typecode tc)
{
	if(tc >= m->ntypes)
		return NULL;
	return m->types[tc].userdata;
}


RCHM_errors rchm_AddBlock(RCHM_manager *m, int bi)
{
	if(!(m->blocktab[bi] = (RCHM_handleinfo *)malloc(
			RCHM_BLOCKSIZE * sizeof(RCHM_handleinfo))))
		return RCHM_OOMEMORY;
	return RCHM_OK;
}


void rchm_Cleanup(RCHM_manager *m)
{
	int i, bi = m->nexthandle >> RCHM_BLOCKSIZE_POW2;
	for(i = 0; i <= bi; ++i)
		free(m->blocktab[i]);
	for(i = 0; i < m->ntypes; ++i)
		free(m->types[i].name);
	free(m->types);
	memset(m, 0, sizeof(m));
}


RCHM_errors rchm_Init(RCHM_manager *m, int inithandles)
{
	RCHM_errors res;
	int i, ii = (inithandles - 1) >> RCHM_BLOCKSIZE_POW2;
	if(ii >= RCHM_MAXBLOCKS)
		return RCHM_OOHANDLES;
	memset(m, 0, sizeof(m));
	m->pool = -1;
	for(i = 0; i < ii; ++i)
		if((res = rchm_AddBlock(m, i)))
		{
			rchm_Cleanup(m);
			return res;
		}
	return RCHM_OK;
}
