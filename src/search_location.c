/***************************************************************************
 *            search_location.c
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

#include <stdlib.h>
#include <gtk/gtk.h>

#include "main.h"
#include "db.h"
#include "util.h"
#include "location.h"
#include "search.h"
#include "searchwindow.h"
#include "search_location.h"

#define MAX_QUERY					(4000)
#define SEARCH_RESULT_COUNT_LIMIT	(100)

//typedef struct {
//	mappoint_t m_ptCenter;
//	gdouble m_fRadiusInDegrees;
//	gint m_nLocationSetID;
//	gchar* m_pszCleanedSentence;
//	gchar** m_aWords;
//	gint m_nWordCount;
//} locationsearch_t;

void search_location_on_cleaned_sentence(const gchar* pszCleanedSentence);
//void search_location_on_words(gchar** aWords, gint nWordCount);
void search_location_filter_result(gint nLocationID, const gchar* pszName, const gchar* pszAddress, const mappoint_t* pCoordinates);

void search_location_execute(const gchar* pszSentence)
{
	g_print("search_location_execute\n");

	TIMER_BEGIN(search, "BEGIN LocationSearch");

	// copy sentence and clean it
	gchar* pszCleanedSentence = g_strdup(pszSentence);
	search_clean_string(pszCleanedSentence);
	search_location_on_cleaned_sentence(pszCleanedSentence);
	g_free(pszCleanedSentence);

	TIMER_END(search, "END LocationSearch");
}

	// Create an array of the words
/*         gchar** aaWords = g_strsplit(pszCleanedSentence," ", 0);        // " " = delimeters, 0 = no max # */
/*         gint nWords = g_strv_length(aaWords);                                                             */
/*         search_location_on_words(aaWords, nWords);                                                        */
/*         g_strfreev(aaWords);    // free entire array of strings                                           */

void search_location_on_cleaned_sentence(const gchar* pszCleanedSentence)
{

	// Get POI #, Name, Address, and Coordinates. Match a POI if any of the words given are in ANY attributes of the POI.
	// NOTE: We're using this behavior (http://dev.mysql.com/doc/mysql/en/fulltext-boolean.html):
	// 'apple banana'
	//   Find rows that contain at least one of the two words.

	gchar* pszSQL = g_strdup_printf(
		"SELECT Location.ID, LocationAttributeValue_Name.Value AS Name, LocationAttributeValue_Address.Value AS Address, AsBinary(Location.Coordinates)"
		" FROM LocationAttributeValue"
		" LEFT JOIN LocationAttributeName ON (LocationAttributeValue.AttributeNameID=LocationAttributeName.ID)"
		" LEFT JOIN Location ON (LocationAttributeValue.LocationID=Location.ID)"
		" LEFT JOIN LocationAttributeValue AS LocationAttributeValue_Name ON (Location.ID=LocationAttributeValue_Name.LocationID AND LocationAttributeValue_Name.AttributeNameID=%d)"
		" LEFT JOIN LocationAttributeValue AS LocationAttributeValue_Address ON (Location.ID=LocationAttributeValue_Address.LocationID AND LocationAttributeValue_Address.AttributeNameID=%d)"
		" WHERE"
		" MATCH(LocationAttributeValue.Value) AGAINST ('%s' IN BOOLEAN MODE)"
		" GROUP BY Location.ID;",
			LOCATION_ATTRIBUTE_ID_NAME,
			LOCATION_ATTRIBUTE_ID_ADDRESS,
			pszCleanedSentence
		);

	db_resultset_t* pResultSet;
	if(db_query(pszSQL, &pResultSet)) {
		db_row_t aRow;

		// get result rows!
		gint nCount = 0;		
		while((aRow = mysql_fetch_row(pResultSet))) {
			nCount++;
			if(nCount <= SEARCH_RESULT_COUNT_LIMIT) {
				gint nLocationID = atoi(aRow[0]);
				gchar* pszLocationName = aRow[1];
				gchar* pszLocationAddress = aRow[2];
				// Parse coordinates
				mappoint_t pt;
				db_parse_wkb_point(aRow[3], &pt);

				search_location_filter_result(nLocationID, pszLocationName, pszLocationAddress, &pt);
			}
		}
		db_free_result(pResultSet);

		if(nCount == 0) {
			g_print("no location search results\n");
		}
		else {
			g_print("%d location results\n", nCount);
		}
	}
	else {
		g_print("search failed\n");
	}
}

/* void search_location_on_words(gchar** aWords, gint nWordCount) */
/* {                                                                                         */
/* }                                                                                         */

#define LOCATION_RESULT_SUGGESTED_ZOOMLEVEL	(7)

void search_location_filter_result(gint nLocationID, const gchar* pszName, const gchar* pszAddress, const mappoint_t* pCoordinates)
{
	gchar* pszResultText = g_strdup_printf("<b>%s</b>%s%s", pszName,
					       (pszAddress == NULL || pszAddress[0] == '\0') ? "" : "\n",
					       (pszAddress == NULL || pszAddress[0] == '\0') ? "" : pszAddress);

	searchwindow_add_result(pszResultText, pCoordinates, LOCATION_RESULT_SUGGESTED_ZOOMLEVEL);

	g_free(pszResultText);
}

// 

/*
void search_location_on_locationsearch_struct(locationsearch_t* pLocationSearch)
{
	// location matching
	gchar* pszCoordinatesMatch = NULL;
	if(TRUE) {
		mappoint_t ptCenter;
		map_get_centerpoint(&ptCenter);

		gdouble fDegrees = pLocationSearch->m_fRadiusInDegrees;
		pszCoordinatesMatch = g_strdup_printf(
			" AND MBRIntersects(GeomFromText('Polygon((%f %f,%f %f,%f %f,%f %f,%f %f))'), Coordinates)",
			ptCenter.m_fLatitude + fDegrees, ptCenter.m_fLongitude - fDegrees, 	// upper left
			ptCenter.m_fLatitude + fDegrees, ptCenter.m_fLongitude + fDegrees, 	// upper right
			ptCenter.m_fLatitude - fDegrees, ptCenter.m_fLongitude + fDegrees, 	// bottom right
			ptCenter.m_fLatitude - fDegrees, ptCenter.m_fLongitude - fDegrees,	// bottom left
			ptCenter.m_fLatitude + fDegrees, ptCenter.m_fLongitude - fDegrees);	// upper left again
	} else {
		pszCoordinatesMatch = g_strdup("");
	}

	// location set matching
	gchar* pszLocationSetMatch = NULL;
	if(pLocationSearch->m_nLocationSetID != 0) {
		pszLocationSetMatch = g_strdup_printf(" AND LocationSetID=%d", pLocationSearch->m_nLocationSetID);		
	}
	else {
		pszLocationSetMatch = g_strdup("");
	}

	// attribute value matching
	gchar* pszAttributeNameJoin = NULL;
	gchar* pszAttributeNameMatch = NULL;
	if(pLocationSearch->m_pszCleanedSentence[0] != '\0') {
		pszAttributeNameJoin = g_strdup_printf("LEFT JOIN LocationAttributeValue ON (LocationAttributeValue.LocationID=Location.ID AND MATCH(LocationAttributeValue.Value) AGAINST ('%s' IN BOOLEAN MODE))", pLocationSearch->m_pszCleanedSentence);		
		pszAttributeNameMatch = g_strdup_printf("AND LocationAttributeValue.ID IS NOT NULL");
	}
	else {
		pszAttributeNameJoin = g_strdup("");
		pszAttributeNameMatch = g_strdup("");
	}
	
	// build the query
	gchar azQuery[MAX_QUERY];
	g_snprintf(azQuery, MAX_QUERY,
		"SELECT Location.ID"
		" FROM Location"
		" %s"	// pszAttributeNameJoin
		" WHERE TRUE"
		" %s"	// pszCoordinatesMatch
		" %s"	// pszLocationSetMatch
		" %s"	// pszAttributeNameMatch
		//" ORDER BY RoadName.Name"
		" LIMIT %d;",
			pszAttributeNameJoin,
			pszCoordinatesMatch,
			pszLocationSetMatch,
			pszAttributeNameMatch,
			SEARCH_RESULT_COUNT_LIMIT + 1);

	// free temp strings
	g_free(pszCoordinatesMatch);
	g_free(pszLocationSetMatch);

	g_print("SQL: %s\n", azQuery);

	db_resultset_t* pResultSet;
	if(db_query(azQuery, &pResultSet)) {
		db_row_t aRow;

		// get result rows!
		gint nCount = 0;		
		while((aRow = mysql_fetch_row(pResultSet))) {
			nCount++;
			if(nCount <= SEARCH_RESULT_COUNT_LIMIT) {
				search_location_filter_result(atoi(aRow[0]));
			}
		}
		db_free_result(pResultSet);

		if(nCount == 0) {
			g_print("no location search results\n");
		}
	}
	else {
		g_print("search failed\n");
	}
}

void search_location_filter_result(gint nLocationID)
{
	g_print("result: %d\n", nLocationID);
	gchar* p = g_strdup_printf("<span size='larger'><b>Happy Garden</b></span>\n145 Main St.\nCambridge, MA 02141\n617-555-1021");
	mappoint_t pt = {0,0};
	searchwindow_add_result(0, p, &pt);
	g_free(p);
}
*/
