/***************************************************************************
 *            pointstring.c
 *
 *  Copyright  2005  Ian McIntosh
 *  ian_mcintosh@linuxadvocate.org
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include "main.h"
#include "map.h"
#include "point.h"
#include "pointstring.h"

#ifdef USE_GFREELIST
#include "gfreelist.h"
GFreeList* g_pPointStringFreeList = NULL;
#else
GMemChunk* g_pPointStringChunkAllocator = NULL;
#endif

void pointstring_init(void)
{
#ifdef USE_GFREELIST
	g_pPointStringFreeList = g_free_list_new(sizeof(pointstring_t), 1000);
#else
	g_pPointStringChunkAllocator = g_mem_chunk_new("ROADSTER pointstrings",
			sizeof(pointstring_t), 1000, G_ALLOC_AND_FREE);
#endif
}

/*******************************************************
** pointstring alloc/free
*******************************************************/

// create a new pointstring
gboolean pointstring_alloc(pointstring_t** ppPointString)
{
	g_return_val_if_fail(ppPointString != NULL, FALSE);
	g_return_val_if_fail(*ppPointString == NULL, FALSE);	// must be a pointer to a NULL pointer

	// allocate it
#ifdef USE_GFREELIST
	g_assert(g_pPointStringFreeList != NULL);
	pointstring_t* pNew = g_free_list_alloc(g_pPointStringFreeList);
	memset(pNew, 0, sizeof(pointstring_t));
#else
	g_assert(g_pPointStringChunkAllocator != NULL);
	pointstring_t* pNew = g_mem_chunk_alloc0(g_pPointStringChunkAllocator);
#endif
	if(pNew) {
		// configure it
		pNew->pPointsArray = g_ptr_array_sized_new(2);
		*ppPointString = pNew;
		return TRUE;
	}
	return FALSE;
}

// return a pointstring struct to the allocator
void pointstring_free(pointstring_t* pPointString)
{
	g_return_if_fail(pPointString != NULL);

	int i;
	for(i = (pPointString->pPointsArray->len - 1) ; i>=0 ; i--) {
		mappoint_t* pPoint = g_ptr_array_remove_index_fast(pPointString->pPointsArray, i);
		point_free(pPoint);
	}
	g_assert(pPointString->pPointsArray->len == 0);

	g_ptr_array_free(pPointString->pPointsArray, TRUE);
	g_free(pPointString->pszName);

#ifdef USE_GFREELIST
	g_free_list_free(g_pPointStringFreeList, pPointString);
#else
	g_mem_chunk_free(g_pPointStringChunkAllocator, pPointString);
#endif
}

// copies pPoint and adds it
void pointstring_append_point(pointstring_t* pPointString, const mappoint_t* pPoint)
{
	mappoint_t* pNewPoint = NULL;
	point_alloc(&pNewPoint);

	memcpy(pNewPoint, pPoint, sizeof(mappoint_t));

	g_ptr_array_add(pPointString->pPointsArray, pNewPoint);
}

void pointstring_debug_print(pointstring_t* pPointString)
{
	int i;
	for(i=0 ; i<pPointString->pPointsArray->len ; i++) {
		mappoint_t* pPoint = g_ptr_array_index(pPointString->pPointsArray, i);
		g_print("(%f, %f)", pPoint->fLatitude, pPoint->fLongitude);
	}
}
