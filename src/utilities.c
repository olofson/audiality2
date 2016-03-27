/*
 * utilities.c - Audiality 2 internal utilities
 *
 * Copyright 2012, 2016 David Olofson <david@olofson.net>
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

#include <stdlib.h>
#include "internals.h"


/*---------------------------------------------------------
	Dynamically allocated table of <name, handle>
---------------------------------------------------------*/

int a2nt_AddItem(A2_nametab *nt, const char *name, A2_handle h)
{
	if(!nt->items || (nt->nitems >= nt->size))
	{
		int nsize = nt->size ? nt->size * 3 / 2 : 8;
		A2_ntitem *ni = (A2_ntitem *)realloc(nt->items,
				nsize * sizeof(A2_ntitem));
		if(!ni)
			return -A2_OOMEMORY;
		nt->items = ni;
		nt->size = nsize;
	}
	if(!(nt->items[nt->nitems].name = strdup(name)))
		return -A2_OOMEMORY;
	nt->items[nt->nitems].handle = h;
	return nt->nitems++;
}

A2_handle a2nt_FindItem(A2_nametab *nt, const char *name)
{
	int len, i;
	const char *sep = strchr(name, '.');
	if(sep)
		len = sep - name;
	else
		len = strlen(name);
	for(i = 0; i < nt->nitems; ++i)
		if(!strncmp(name, nt->items[i].name, len) &&
				(strlen(nt->items[i].name) == len))
			return nt->items[i].handle;
	return -1;
}

int a2nt_FindItemByHandle(A2_nametab *nt, A2_handle h)
{
	int i;
	for(i = 0; i < nt->nitems; ++i)
		if(nt->items[i].handle == h)
			return i;
	return -1;
}

void a2nt_Cleanup(A2_nametab *nt)
{
	int i;
	for(i = 0; i < nt->nitems; ++i)
		free(nt->items[i].name);
	free(nt->items);
	memset(nt, 0, sizeof(A2_nametab));
}


/*---------------------------------------------------------
	Dynamically allocated table of handles
---------------------------------------------------------*/

int a2ht_AddItem(A2_handletab *ht, A2_handle h)
{
	if(!ht->items || (ht->nitems >= ht->size))
	{
		int nsize = ht->size ? ht->size * 3 / 2 : 8;
		A2_handle *nh = (A2_handle *)realloc(ht->items,
				nsize * sizeof(A2_handle));
		if(!nh)
			return -A2_OOMEMORY;
		ht->items = nh;
		ht->size = nsize;
	}
	ht->items[ht->nitems] = h;
	return ht->nitems++;
}

int a2ht_FindItem(A2_handletab *ht, A2_handle h)
{
	int i;
	for(i = 0; i < ht->nitems; ++i)
		if(ht->items[i] == h)
			return i;
	return -1;
}

void a2ht_Cleanup(A2_handletab *ht)
{
	free(ht->items);
	memset(ht, 0, sizeof(A2_handletab));
}
