/***************************************************************************
 *            search_road.c
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

/*
Purpose of search_road.c:
 - Match search string against road map objects (XXX: and other map objects?)
*/

#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "db.h"
#include "util.h"
#include "search.h"
#include "search_road.h"
#include "road.h"
#include "glyph.h"
#include "searchwindow.h"		// for defines about glyph size

#define ROAD_RESULT_SUGGESTED_ZOOMLEVEL		(37)

#define FORMAT_ROAD_RESULT_WITHOUT_NUMBER 	("%s %s\n%s")
#define FORMAT_ROAD_RESULT_WITH_NUMBER 		("%d %s %s\n%s")

typedef struct {
	gint nNumber;			// house number	eg. 51
	gchar* pszRoadName;	// road name eg. "Washington"
	gint nCityID;			//
	gint nStateID;
	gchar* pszZIPCode;
	gint nSuffixID;		// a number representing eg. Ave
} roadsearch_t;

#define ROADSEARCH_NUMBER_NONE			(-1)
#define SEARCH_RESULT_COUNT_LIMIT		(400)		// how many rows to get from DB
#define MAX_QUERY 						(4000)

static glyph_t* g_SearchResultTypeRoadGlyph = NULL;


//#define ROAD_MIN_LENGTH_FOR_WILDCARD_SEARCH	(4)	  wildcard search no longer used

gboolean search_address_match_zipcode(const gchar* pszWord)
{
	// very US-centric right now
	gint nLen = strlen(pszWord);
	if(nLen < 4 || nLen > 5) return FALSE;

	gint i;
	for(i=0 ; i<nLen ; i++) {
		if(!g_ascii_isdigit(pszWord[i])) {
			return FALSE;
		}
	}
	return TRUE;
}

// prototypes

GList *search_road_on_words(gchar** aWords, gint nWordCount, GList *ret);
GList *search_road_on_roadsearch_struct(const roadsearch_t* pRoadSearch, GList *ret);
GList *search_road_filter_result(const gchar* pszRoadName, gint nRoadNumber, gint nRoadSuffixID, gint nAddressLeftStart, gint nAddressLeftEnd, gint nAddressRightStart, gint nAddressRightEnd, const gchar* pszCityNameLeft, const gchar* pszCityNameRight, const gchar* pszStateNameLeft, const gchar* pszStateNameRight, const gchar* pszZIPLeft, const gchar* pszZIPRight, GArray* pMapPointsArray, GList *ret);
GList *search_road_add_result(char *text, mappoint_t *pt, GList *ret);

// functions

GList *search_road_execute(const gchar* pszSentence)
{
	GList *ret = NULL;

	// Create an array of the words
	gchar** aWords = g_strsplit(pszSentence," ", 0);	// " " = delimeters, 0 = no max #
	gint nWordCount = g_strv_length(aWords);

	if(nWordCount > 0) {
		ret = search_road_on_words(aWords, nWordCount, ret);
	}

	// cleanup
	g_strfreev(aWords);	// free the array of strings	

	return ret;
}

GList *search_road_on_words(gchar** aWords, gint nWordCount, GList *ret)
{
	g_assert(nWordCount > 0);
	roadsearch_t roadsearch = {0};

	// index of first and last words of road name (the bit in the middle)
	gint iFirst = 0;
	gint iLast = nWordCount-1;

	// Start stripping off words as we identify roadsearch_t structure members
	gint nRemainingWordCount = nWordCount;

	// Claim house number if present
	roadsearch.nNumber = ROADSEARCH_NUMBER_NONE;
	if(nRemainingWordCount >= 2) {
		if(search_address_number_atoi(aWords[iFirst], &roadsearch.nNumber)) {
			iFirst++;	// first word is TAKEN :)
			nRemainingWordCount--;
		}
	}

	// Claim zip code, if present
	if(search_address_match_zipcode(aWords[iLast])) {
		//g_print("matched ZIP %s\n", aWords[iLast]);
		roadsearch.pszZIPCode = aWords[iLast];

		iLast--;	// last word taken
		nRemainingWordCount--;
	}

	if(nRemainingWordCount == 0) {
		// do a zip code search and return
		g_print("TODO: zip code search\n");
		return ret;
	}

	// See if we can match a state name   XXX: We need to match multi-word state names
	gboolean bGotStateName = FALSE;
	if(nRemainingWordCount >= 3) {
		// try two-word state name
		gchar* pszStateName = util_g_strjoinv_limit(" ", aWords, iLast-1, iLast);
		//g_print("trying two-word state name '%s'\n", pszStateName);

		if(db_state_get_id(pszStateName, &roadsearch.nStateID)) {
			//g_print("matched state name!\n");
			iLast -= 2;	// last TWO words taken
			nRemainingWordCount -= 2;
		}
		g_free(pszStateName);
	}
	// try a one-word state name
	if(bGotStateName == FALSE && nRemainingWordCount >= 2) {
		//g_print("trying one-word state name '%s'\n", aWords[iLast]);
		if(db_state_get_id(aWords[iLast], &roadsearch.nStateID)) {
			//g_print("matched state name!\n");
			iLast--;	// last word taken
			nRemainingWordCount--;
		}
	}

	// try to match city name
	gint nCityNameLength;
	for(nCityNameLength = 5 ; nCityNameLength >= 1 ; nCityNameLength--) {
		if(nRemainingWordCount > nCityNameLength) {
			gchar* pszCityName = util_g_strjoinv_limit(" ", aWords, iLast - (nCityNameLength-1), iLast);

			if(db_city_get_id(pszCityName, roadsearch.nStateID, &roadsearch.nCityID)) {
				iLast -= nCityNameLength;	// several words taken :)
				nRemainingWordCount -= nCityNameLength;

				// success
				g_free(pszCityName);
				break;
			}
			else {
				// failure
				g_free(pszCityName);
				continue;
			}
		}
	}

	// road name suffix (eg. "ave")
	if(nRemainingWordCount >= 2) {
		gint nSuffixID;

		if(road_suffix_atoi(aWords[iLast], &nSuffixID)) {
			// matched
			roadsearch.nSuffixID = nSuffixID;
			iLast--;
			nRemainingWordCount--;
		}
	}

	if(nRemainingWordCount > 0) {
		roadsearch.pszRoadName = util_g_strjoinv_limit(" ", aWords, iFirst, iLast);

		ret = search_road_on_roadsearch_struct(&roadsearch, ret);
	}
	else {
		// oops, no street name
		//g_print("no street name found in search\n");
	}
	g_free(roadsearch.pszRoadName);

	return ret;
}

GList *search_road_on_roadsearch_struct(const roadsearch_t* pRoadSearch, GList *ret)
{
	//
	// Assemble the various optional clauses for the SQL statement
	//
	gchar* pszAddressClause;
	if(pRoadSearch->nNumber != ROADSEARCH_NUMBER_NONE) {
		pszAddressClause = g_strdup_printf(
			" AND ("
			"(%d BETWEEN Road0.AddressLeftStart AND Road0.AddressLeftEnd)"
			" OR (%d BETWEEN Road0.AddressLeftEnd AND Road0.AddressLeftStart)"
			" OR (%d BETWEEN Road0.AddressRightStart AND Road0.AddressRightEnd)"
			" OR (%d BETWEEN Road0.AddressRightEnd AND Road0.AddressRightStart)"
			")", pRoadSearch->nNumber, pRoadSearch->nNumber,
				 pRoadSearch->nNumber, pRoadSearch->nNumber);
	}
	else {
		pszAddressClause = g_strdup("");
	}

	gchar* pszSuffixClause;
	if(pRoadSearch->nSuffixID != ROAD_SUFFIX_NONE) {
		pszSuffixClause = g_strdup_printf(
			" AND (RoadName.SuffixID = %d)",
			pRoadSearch->nSuffixID);
	}
	else {
		pszSuffixClause = g_strdup("");
	}

	gchar* pszZIPClause;
	if(pRoadSearch->pszZIPCode != NULL) {
		gchar* pszSafeZIP = db_make_escaped_string(pRoadSearch->pszZIPCode);
		pszZIPClause = g_strdup_printf(" AND (Road0.ZIPCodeLeft='%s' OR Road0.ZIPCodeRight='%s')", pszSafeZIP, pszSafeZIP);
		db_free_escaped_string(pszSafeZIP);
	}
	else {
		pszZIPClause = g_strdup("");
	}

	gchar* pszCityClause;
	if(pRoadSearch->nCityID != 0) {
		pszCityClause = g_strdup_printf(" AND (Road0.CityLeftID=%d OR Road0.CityRightID=%d)", pRoadSearch->nCityID, pRoadSearch->nCityID);
	}
	else {
		pszCityClause = g_strdup("");
	}

	gchar* pszStateClause;
	if(pRoadSearch->nStateID != 0) {
		pszStateClause = g_strdup_printf(" AND (CityLeft.StateID=%d OR CityRight.StateID=%d)", pRoadSearch->nStateID, pRoadSearch->nStateID);
	}
	else {
		pszStateClause = g_strdup("");
	}

	// if doing a road search, only show 1 hit per road?
	//~ gchar* pszGroupClause;
	//~ if(pRoadSearch->nSuffixID == ROAD_SUFFIX_NONE) {
		//~ pszGroupClause = g_strdup("GROUP BY (RoadName.ID, RoadName.SuffixID)");
	//~ }
	//~ else {
		//~ pszGroupClause = g_strdup("");
	//~ }

	gchar* pszSafeRoadName = db_make_escaped_string(pRoadSearch->pszRoadName);
	//g_print("pRoadSearch->pszRoadName = %s, pszSafeRoadName = %s\n", pRoadSearch->pszRoadName, pszSafeRoadName);

	// XXX: Should we use soundex()? (http://en.wikipedia.org/wiki/Soundex)
	gchar* pszRoadNameCondition = g_strdup_printf("RoadName.Name='%s'", pszSafeRoadName);

	// Now we use only Soundex
	//pszRoadNameCondition = g_strdup_printf("RoadName.NameSoundex = SUBSTRING(SOUNDEX('%s') FROM 1 FOR 10)", pszSafeRoadName);

	gchar* pszQuery = g_strdup_printf(
		"SELECT 0 AS ID, RoadName.Name, RoadName.SuffixID, AsBinary(Road0.Coordinates), Road0.AddressLeftStart, Road0.AddressLeftEnd, Road0.AddressRightStart, Road0.AddressRightEnd, CityLeft.Name, CityRight.Name"
		", StateLeft.Code, StateRight.Code, Road0.ZIPCodeLeft, Road0.ZIPCodeRight"
		" FROM RoadName"
		" LEFT JOIN Road0 ON (RoadName.ID=Road0.RoadNameID%s)"					// address # clause
		// left side
	    " LEFT JOIN City AS CityLeft ON (Road0.CityLeftID=CityLeft.ID)"
		" LEFT JOIN State AS StateLeft ON (CityLeft.StateID=StateLeft.ID)"
		// right side
		" LEFT JOIN City AS CityRight ON (Road0.CityRightID=CityRight.ID)"
		" LEFT JOIN State AS StateRight ON (CityRight.StateID=StateRight.ID)"
		" WHERE %s"
//		" WHERE RoadName.Name='%s'"
		" AND Road0.TypeID IS NOT NULL"	// (any field will do) don't include rows where the Road didn't match
		// begin clauses
		"%s"
		"%s"
		"%s"
		"%s"
		" LIMIT %d;",
			   pszAddressClause,
			
			   pszRoadNameCondition,

			   // clauses
			   pszSuffixClause,
			   pszZIPClause,
			   pszCityClause,
			   pszStateClause,
			SEARCH_RESULT_COUNT_LIMIT + 1);

	// free intermediate strings
	db_free_escaped_string(pszSafeRoadName);
	g_free(pszAddressClause);
	g_free(pszRoadNameCondition);
	g_free(pszSuffixClause);
	g_free(pszZIPClause);
	g_free(pszCityClause);
	g_free(pszStateClause);
	
	//g_print("SQL: %s\n", azQuery);

	db_resultset_t* pResultSet;
	if(db_query(pszQuery, &pResultSet)) {
		db_row_t aRow;

		// get result rows!
		gint nCount = 0;		
		while((aRow = db_fetch_row(pResultSet))) {
			// [0] Road.ID
			// [1] RoadName.Name
			// [2] RoadName.SuffixID
			// [3] AsBinary(Road.Coordinates),
			// [4] Road.AddressLeftStart,
			// [5] Road.AddressLeftEnd
			// [6] Road.AddressRightStart
			// [7] Road.AddressRightEnd,
			// [8] CityLeft.Name,
			// [9] CityRight.Name

			// [10] StateLeft.Name,
			// [11] StateRight.Name

			// [12] ZIPLeft
			// [13] ZIPRight

			nCount++;
			if(nCount <= SEARCH_RESULT_COUNT_LIMIT) {

				GArray* pMapPointsArray = g_array_new(FALSE, FALSE, sizeof(mappoint_t));
				maprect_t r;
				db_parse_wkb_linestring((gint8 *)aRow[3], pMapPointsArray, &r);
				ret = search_road_filter_result(aRow[1], pRoadSearch->nNumber, atoi(aRow[2]), atoi(aRow[4]), atoi(aRow[5]), atoi(aRow[6]), atoi(aRow[7]), aRow[8], aRow[9], aRow[10], aRow[11], aRow[12], aRow[13], pMapPointsArray, ret);
				//g_print("%03d: Road.ID='%s' RoadName.Name='%s', Suffix=%s, L:%s-%s, R:%s-%s\n", nCount, aRow[0], aRow[1], aRow[3], aRow[4], aRow[5], aRow[6], aRow[7]);

				g_array_free(pMapPointsArray, TRUE);
			}
		}
		db_free_result(pResultSet);
	}
	else {
		g_print("road search failed\n");
	}
	g_free(pszQuery);

	return ret;
}

// XXX: doesn't map.c have something like this? :)
static gfloat point_calc_distance(mappoint_t* pA, mappoint_t* pB)
{
	gdouble fRise = pB->fLatitude - pA->fLatitude;
	gdouble fRun = pB->fLongitude - pA->fLongitude;
	return sqrt((fRun*fRun) + (fRise*fRise));
}

typedef enum {
	ROADSIDE_CENTER,
	ROADSIDE_LEFT,
	ROADSIDE_RIGHT,
} ERoadSide;

#define HIGHLIGHT_DISTANCE_FROM_ROAD (0.00012)		// this seems like a good amount...

static void mappoint_array_walk_percentage(GArray* pMapPointsArray, gdouble fPercent, ERoadSide eRoadSide, mappoint_t* pReturnPoint)
{
	gint i;
	if(pMapPointsArray->len < 2) {
		g_assert_not_reached();
	}

	//
	// count total distance
	//
	gfloat fTotalDistance = 0.0;
	mappoint_t* pPointA = &g_array_index(pMapPointsArray, mappoint_t, 0);
	mappoint_t* pPointB;
	for(i=1 ; i<pMapPointsArray->len ; i++) {
		pPointB = &g_array_index(pMapPointsArray, mappoint_t, 1);

		fTotalDistance += point_calc_distance(pPointA, pPointB);

		pPointA = pPointB;
	}

	gfloat fTargetDistance = (fTotalDistance * fPercent);
	gfloat fRemainingDistance = fTargetDistance;

	pPointA = &g_array_index(pMapPointsArray, mappoint_t, 0);
	for(i=1 ; i<pMapPointsArray->len ; i++) {
		pPointB = &g_array_index(pMapPointsArray, mappoint_t, 1);

		gfloat fLineSegmentDistance = point_calc_distance(pPointA, pPointB);
		if(fRemainingDistance <= fLineSegmentDistance || (i == pMapPointsArray->len-1)) {
			// this is the line segment we are looking for.

			gfloat fPercentOfLine = (fRemainingDistance / fLineSegmentDistance);
			if(fPercentOfLine > 1.0) fPercentOfLine = 1.0;

			gfloat fRise = (pPointB->fLatitude - pPointA->fLatitude);
			gfloat fRun = (pPointB->fLongitude - pPointA->fLongitude);

			// Calculate the a point on the center of the line segment
			// the right percent along the road
			pReturnPoint->fLongitude = pPointA->fLongitude + (fRun * fPercentOfLine);
			pReturnPoint->fLatitude = pPointA->fLatitude + (fRise * fPercentOfLine);

			gdouble fPerpendicularNormalizedX = -fRise / fLineSegmentDistance;
			gdouble fPerpendicularNormalizedY = fRun / fLineSegmentDistance;

			if(eRoadSide == ROADSIDE_CENTER) {
				// do nothing, we're already at line center
			}
			else if(eRoadSide == ROADSIDE_LEFT) {
// g_print("MOVING LEFT\n");
				pReturnPoint->fLongitude += fPerpendicularNormalizedX * HIGHLIGHT_DISTANCE_FROM_ROAD;
				pReturnPoint->fLatitude += fPerpendicularNormalizedY * HIGHLIGHT_DISTANCE_FROM_ROAD;
			}
			else {
// g_print("MOVING RIGHT\n");
				pReturnPoint->fLongitude -= fPerpendicularNormalizedX * HIGHLIGHT_DISTANCE_FROM_ROAD;
				pReturnPoint->fLatitude -= fPerpendicularNormalizedY * HIGHLIGHT_DISTANCE_FROM_ROAD;
			}
			return;
		}
		fRemainingDistance -= fLineSegmentDistance;
		pPointA = pPointB;
	}
	g_assert_not_reached();
}

#if ROADSTER_DEAD_CODE
static gint min4(gint a, gint b, gint c, gint d)
{
	gint x = min(a,b);
	gint y = min(c,d);
	return min(x,y);
}

static gint max4(gint a, gint b, gint c, gint d)
{
	gint x = max(a,b);
	gint y = max(c,d);
	return max(x,y);
}
#endif /* ROADSTER_DEAD_CODE */

//
// XXX: the SQL doesn't require all fields be set for THE SAME SIDE
// 		do we need to filter out records where each side matches some of the criteria but not all?
//
#define BUFFER_SIZE 200
GList *search_road_filter_result(
		const gchar* pszRoadName, gint nRoadNumber, gint nRoadSuffixID,
		gint nAddressLeftStart, gint nAddressLeftEnd,
		gint nAddressRightStart, gint nAddressRightEnd,
		const gchar* pszCityNameLeft, const gchar* pszCityNameRight,
		const gchar* pszStateNameLeft, const gchar* pszStateNameRight,
		const gchar* pszZIPLeft, const gchar* pszZIPRight,
		GArray* pMapPointsArray, GList *ret)
{
	gchar azBuffer[BUFFER_SIZE];

	mappoint_t ptAddress = {0};

	if (!g_SearchResultTypeRoadGlyph)
		g_SearchResultTypeRoadGlyph = glyph_load_at_size("search-result-type-road",
		                                                 SEARCHWINDOW_SEARCH_RESULT_GLYPH_WIDTH,
		                                                 SEARCHWINDOW_SEARCH_RESULT_GLYPH_HEIGHT);

//     pszStateNameLeft = "(st)";
//     pszStateNameRight = "(st)";

	// set City, State, Zip text to be used for each side of the road "City, State, ZIP"
	gchar* pszCSZLeft = g_strdup_printf("%s%s%s%s%s",
										(pszCityNameLeft != NULL) ? pszCityNameLeft : "",
										(pszStateNameLeft != NULL) ? ", " : "",
										(pszStateNameLeft != NULL) ? pszStateNameLeft : "",
										(strcmp(pszZIPLeft, "00000") == 0) ? "" : ", ",
										(strcmp(pszZIPLeft, "00000") == 0) ? "" : pszZIPLeft);
	gchar* pszCSZRight = g_strdup_printf("%s%s%s%s%s",
										 (pszCityNameRight != NULL) ? pszCityNameRight : "",
										 (pszStateNameRight != NULL) ? ", " : "",
										 (pszStateNameRight != NULL) ? pszStateNameRight : "",
										 (strcmp(pszZIPRight, "00000") == 0) ? "" : ", ",
										 (strcmp(pszZIPRight, "00000") == 0) ? "" : pszZIPRight);
	
	// consider the longer of the two to be better-- (for joined results)
//	gchar* pszCSZBetter = (strlen(pszCSZLeft) > strlen(pszCSZRight)) ? pszCSZLeft : pszCSZRight;

	if(nRoadNumber == ROADSEARCH_NUMBER_NONE) {
		// Right in the center
		mappoint_array_walk_percentage(pMapPointsArray, 0.5, ROADSIDE_CENTER, &ptAddress);

//         gint nStart = min4(nAddressLeftStart, nAddressLeftEnd, nAddressRightStart, nAddressRigtEnd);
//         gint nEnd = min4(nAddressLeftStart, nAddressLeftEnd, nAddressRightStart, nAddressRigtEnd);

/*
		if(nAddressRightStart == 0 && nAddressRightEnd == 0) {
*/
			// show no numbers if they're both 0
			g_snprintf(azBuffer, BUFFER_SIZE, FORMAT_ROAD_RESULT_WITHOUT_NUMBER,
					   pszRoadName,
					   road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG),
					   pszCSZRight);
/*
		}
		else if(nAddressRightStart < nAddressRightEnd) {
			g_snprintf(azBuffer, BUFFER_SIZE, "%d-%d %s %s\n%s", nAddressRightStart, nAddressRightEnd, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZRight);
		}
		else {
			// reverse start/end for the dear user :)
			g_snprintf(azBuffer, BUFFER_SIZE, "%d-%d %s %s\n%s", nAddressRightEnd, nAddressRightStart, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZRight);
		}
		searchwindow_add_result(SEARCH_RESULT_TYPE_ROAD, azBuffer, &ptAddress, ROAD_RESULT_SUGGESTED_ZOOMLEVEL);

		// do left side, same as right side (see above)
		if(nAddressLeftStart == 0 && nAddressLeftEnd == 0) {
			g_snprintf(azBuffer, BUFFER_SIZE, "%s %s\n%s", pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZLeft);
		}
		else if(nAddressLeftStart < nAddressLeftEnd) {
			g_snprintf(azBuffer, BUFFER_SIZE, "%d-%d %s %s\n%s", nAddressLeftStart, nAddressLeftEnd, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZLeft);
		}
		else {
			// swap address to keep smaller number to the left
			g_snprintf(azBuffer, BUFFER_SIZE, "%d-%d %s %s\n%s", nAddressLeftEnd, nAddressLeftStart, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZLeft);
		}
*/		
		ret = search_road_add_result(azBuffer, &ptAddress, ret);		
	}
	else {	// else the search had a road number
		// NOTE: we have to filter out results like "97-157" when searching for "124" because it's
		// on the wrong side of the road.
//g_snprintf(azBuffer, BUFFER_SIZE, "%d %s %s", nRoadNumber, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG));

		// check left side of street
		// NOTE: if search was for an even, at least one (and hopefully both) of the range should be even
		if((is_even(nRoadNumber) && (is_even(nAddressLeftStart) || is_even(nAddressLeftEnd))) ||
		   (is_odd(nRoadNumber) && (is_odd(nAddressLeftStart) || is_odd(nAddressLeftEnd))))
		{
			// check range, and the range reversed
			if(nRoadNumber >= nAddressLeftStart && nRoadNumber <= nAddressLeftEnd) {
				// MATCH: left side forward
				gint nRange = (nAddressLeftEnd - nAddressLeftStart);
				if(nRange == 0) {
					// just use road center...?
					mappoint_array_walk_percentage(pMapPointsArray, 0.5, ROADSIDE_LEFT, &ptAddress);
				}
				else {
					gfloat fPercent = (gfloat)(nRoadNumber - nAddressLeftStart) / (gfloat)nRange;
					mappoint_array_walk_percentage(pMapPointsArray, fPercent, ROADSIDE_LEFT, &ptAddress);
				}
				g_snprintf(azBuffer, BUFFER_SIZE, FORMAT_ROAD_RESULT_WITH_NUMBER, nRoadNumber, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZLeft);
				ret = search_road_add_result(azBuffer, &ptAddress, ret);				
			}
			else if(nRoadNumber >= nAddressLeftEnd && nRoadNumber <= nAddressLeftStart) {
				// MATCH: left side backwards
				gint nRange = (nAddressLeftStart - nAddressLeftEnd);
				if(nRange == 0) {
					// just use road center...?
					mappoint_array_walk_percentage(pMapPointsArray, 0.5, ROADSIDE_RIGHT, &ptAddress);
				}
				else {
					gfloat fPercent = (gfloat)(nRoadNumber - nAddressLeftEnd) / (gfloat)nRange;

					// flip percent (23 becomes 77, etc.)
					mappoint_array_walk_percentage(pMapPointsArray, (100.0 - fPercent), ROADSIDE_RIGHT, &ptAddress);
				}
				g_snprintf(azBuffer, BUFFER_SIZE, FORMAT_ROAD_RESULT_WITH_NUMBER, nRoadNumber, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZLeft);
				ret = search_road_add_result(azBuffer, &ptAddress, ret);
			}
		}

		// check right side of street
		if((is_even(nRoadNumber) && (is_even(nAddressRightStart) || is_even(nAddressRightEnd))) ||
		   (is_odd(nRoadNumber) && (is_odd(nAddressRightStart) || is_odd(nAddressRightEnd))))
		{
			// check range, and the range reversed
			if(nRoadNumber >= nAddressRightStart && nRoadNumber <= nAddressRightEnd) {
				// MATCH: right side forward
				gint nRange = (nAddressRightEnd - nAddressRightStart);
				if(nRange == 0) {
					// just use road center...?
					mappoint_array_walk_percentage(pMapPointsArray, 0.5, ROADSIDE_RIGHT, &ptAddress);
				}
				else {
					gfloat fPercent = (gfloat)(nRoadNumber - nAddressRightStart) / (gfloat)nRange;
					mappoint_array_walk_percentage(pMapPointsArray, fPercent, ROADSIDE_RIGHT, &ptAddress);
				}
				g_snprintf(azBuffer, BUFFER_SIZE, FORMAT_ROAD_RESULT_WITH_NUMBER, nRoadNumber, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZRight);
				ret = search_road_add_result(azBuffer, &ptAddress, ret);
			}
			else if(nRoadNumber >= nAddressRightEnd && nRoadNumber <= nAddressRightStart) {
				// MATCH: right side backwards
				gint nRange = (nAddressRightStart - nAddressRightEnd);
				if(nRange == 0) {
					// just use road center...?
					mappoint_array_walk_percentage(pMapPointsArray, 0.5, ROADSIDE_LEFT, &ptAddress);
				}
				else {
					gfloat fPercent = (gfloat)(nRoadNumber - nAddressRightEnd) / (gfloat)nRange;

					// flip percent (23 becomes 77, etc.)
					mappoint_array_walk_percentage(pMapPointsArray, (100.0 - fPercent), ROADSIDE_LEFT, &ptAddress);
				}
				g_snprintf(azBuffer, BUFFER_SIZE, FORMAT_ROAD_RESULT_WITH_NUMBER, nRoadNumber, pszRoadName, road_suffix_itoa(nRoadSuffixID, ROAD_SUFFIX_LENGTH_LONG), pszCSZRight);
				ret = search_road_add_result(azBuffer, &ptAddress, ret);
			}
		}
	}
	g_free(pszCSZLeft);
	g_free(pszCSZRight);

	return ret;
}

GList *search_road_add_result(char *text, mappoint_t *pt, GList *ret)
{
	mappoint_t *point = g_new0(mappoint_t, 1);
	*point = *pt;

	struct search_result *hit = g_new0(struct search_result, 1);
	*hit = (struct search_result) {
		.type       = SEARCH_RESULT_TYPE_ROAD,
		.text       = g_strdup(text),
		.glyph      = g_SearchResultTypeRoadGlyph,
		.point      = point,
		.zoom_level = ROAD_RESULT_SUGGESTED_ZOOMLEVEL,
	};

	ret = g_list_append(ret, hit);

	return ret;
}
