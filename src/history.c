/***************************************************************************
 *            history.c
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
#include "history.h"
#include "map.h"

typedef struct {
	mappoint_t m_MapPoint;
	gint m_nZoomLevel;
} mapview_t;

void history_init(void)
{
	// module init
}

history_t* history_new()
{
	history_t* pNew = g_new0(history_t, 1);
	pNew->m_MapViewArray = g_array_new(FALSE, FALSE, sizeof(mapview_t));
	pNew->m_nCurrentIndex = -1;
	return pNew;
}

void history_add(history_t* pHistory, mappoint_t* pPoint, gint nZoomLevel)
{
	g_assert(pHistory != NULL);
	g_assert(pPoint != NULL);

	if(pHistory->m_nCurrentIndex < (pHistory->m_MapViewArray->len - 1)) {
		// clear out everything after currentindex in the array!
		g_array_remove_range(pHistory->m_MapViewArray, pHistory->m_nCurrentIndex + 1, (pHistory->m_MapViewArray->len - pHistory->m_nCurrentIndex) - 1);

		pHistory->m_nTotalItems = (pHistory->m_nCurrentIndex + 1);
	}

	// Move to next one
	pHistory->m_nCurrentIndex++;

	if(pHistory->m_nCurrentIndex >= pHistory->m_MapViewArray->len) {
		// XXX: is this doing a realloc every time?  ouch. :)
		g_array_set_size(pHistory->m_MapViewArray, pHistory->m_MapViewArray->len + 1);
	}

	// Get pointer to it
	mapview_t* pNew = &g_array_index(pHistory->m_MapViewArray, mapview_t, pHistory->m_nCurrentIndex);
	g_return_if_fail(pNew != NULL);

	// Save details
	memcpy(&(pNew->m_MapPoint), pPoint, sizeof(mappoint_t));
	pNew->m_nZoomLevel = nZoomLevel;

	pHistory->m_nTotalItems++;
}

gboolean history_can_go_forward(history_t* pHistory)
{
	return(pHistory->m_nCurrentIndex < (pHistory->m_nTotalItems - 1));
}

gboolean history_can_go_back(history_t* pHistory)
{
	return(pHistory->m_nCurrentIndex > 0);
}

gboolean history_go_forward(history_t* pHistory)
{
	if(history_can_go_forward(pHistory)) {
		pHistory->m_nCurrentIndex++;
		return TRUE;
	}
}

gboolean history_go_back(history_t* pHistory)
{
	if(history_can_go_back(pHistory)) {
		pHistory->m_nCurrentIndex--;
		return TRUE;
	}
}

void history_get_current(history_t* pHistory, mappoint_t* pReturnPoint, gint* pnReturnZoomLevel)
{
	g_assert(pHistory != NULL);
	g_assert(pReturnPoint != NULL);
	g_assert(pnReturnZoomLevel != NULL);
	g_assert(pHistory->m_nCurrentIndex >= 0);
	g_assert(pHistory->m_nCurrentIndex < pHistory->m_nTotalItems);

	mapview_t* pCurrent = &g_array_index(pHistory->m_MapViewArray, mapview_t, pHistory->m_nCurrentIndex);
	
	memcpy(pReturnPoint, &(pCurrent->m_MapPoint), sizeof(mappoint_t));
	*pnReturnZoomLevel  = pCurrent->m_nZoomLevel;
}