/***************************************************************************
 *            db.c
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

#include <mysql.h>
#include <glib.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "db.h"
#include "mainwindow.h"
#include "util.h"
#include "location.h"
#include "locationset.h"

#ifdef USE_GNOME_VFS
#include <gnome-vfs-2.0/libgnomevfs/gnome-vfs.h>
#endif

#define MYSQL_RESULT_SUCCESS  	(0)		// for clearer code

#define MYSQL_ERROR_DUPLICATE_KEY	(1062)

#define MAX_SQLBUFFER_LEN		(132000)	// must be big for lists of coordinates
#define COORD_LIST_MAX 			(128000)

// mysql_use_result - 	less client memory, ties up the server (other clients can't do updates)
//						better for embedded or local servers
// mysql_store_result - more client memory, gets all results right away and frees up server
//						better for remote servers
#define MYSQL_GET_RESULT(x)		mysql_store_result((x))

db_connection_t* g_pDB = NULL;


/******************************************************
** Init and deinit of database module
******************************************************/

// call once on program start-up
void db_init()
{
	gchar* pszSetQueryCacheSize = g_strdup_printf("--query-cache-size=%dMB", 40);
	gchar* pszKeyBufferSize	= g_strdup_printf("--key-buffer-size=%dMB", 32);

	gchar* apszServerOptions[] = {
		"",	// program name -- unused

		// Unused server features
		"--skip-innodb",
		"--skip-bdb",

		// query cache options
		"--query-cache-type=1",		// enable query cache (for map tiles)
		pszSetQueryCacheSize,

		// fulltext index options
		"--ft-min-word-len=1",		// don't miss any words, even 1-letter words (esp. numbers like "3")
		"--ft-stopword-file=''",	// non-existant stopword file. we don't want ANY stopwords (words that are ignored)

		// Misc options
		pszKeyBufferSize
	};

 	if(mysql_server_init(G_N_ELEMENTS(apszServerOptions), apszServerOptions, NULL) != 0) {
		return;
	}
	g_free(pszSetQueryCacheSize);
	g_free(pszKeyBufferSize);
}

// call once on program shut-down
void db_deinit()
{
	mysql_server_end();
}

gboolean db_query(const gchar* pszSQL, db_resultset_t** ppResultSet)
{
	g_assert(pszSQL != NULL);
	if(g_pDB == NULL) return FALSE;

	gint nResult = mysql_query(g_pDB->pMySQLConnection, pszSQL);
	if(nResult != MYSQL_RESULT_SUCCESS) {
		gint nErrorNumber = mysql_errno(g_pDB->pMySQLConnection);

		// show an error (except for duplicate key 'error', which is common)
		if(nErrorNumber != MYSQL_ERROR_DUPLICATE_KEY) {
			g_warning("db_query: %d:%s (SQL: %s)\n", mysql_errno(g_pDB->pMySQLConnection), mysql_error(g_pDB->pMySQLConnection), pszSQL);
		}
		return FALSE;
	}

	// get result?
	if(ppResultSet != NULL) {
		*ppResultSet = (db_resultset_t*)MYSQL_GET_RESULT(g_pDB->pMySQLConnection);
	}
	return TRUE;
}

db_row_t db_fetch_row(db_resultset_t* pResultSet)
{
	return (db_row_t)mysql_fetch_row((MYSQL_RES*)pResultSet);
}

void db_free_result(db_resultset_t* pResultSet)
{
	mysql_free_result((MYSQL_RES*)pResultSet);
}

gint db_get_last_insert_id()
{
	if(g_pDB == NULL) return 0;
	return mysql_insert_id(g_pDB->pMySQLConnection);
}

/******************************************************
** Connection creation and destruction
******************************************************/

// initiate a new connection to server
gboolean db_connect(const gchar* pzHost, const gchar* pzUserName, const gchar* pzPassword, const gchar* pzDatabase)
{
	// create a MySQL connection context
	MYSQL *pMySQLConnection = mysql_init(NULL);
	g_return_val_if_fail(pMySQLConnection != NULL, FALSE);

	// attempt a MySQL connection
	if(mysql_real_connect(pMySQLConnection, pzHost, pzUserName, pzPassword, pzDatabase, 0, NULL, 0) == FALSE) {
		g_warning("mysql_real_connect failed: %s\n", mysql_error(pMySQLConnection));
		return FALSE;
	}

	// on success, alloc our connection struct and fill it
	db_connection_t* pNewConnection = g_new0(db_connection_t, 1);
	pNewConnection->pMySQLConnection = pMySQLConnection;
	pNewConnection->pzHost = g_strdup(pzHost);
	pNewConnection->pzUserName = g_strdup(pzUserName);
	pNewConnection->pzPassword = g_strdup(pzPassword);
	pNewConnection->pzDatabase = g_strdup(pzDatabase);

	g_assert(g_pDB == NULL);
	g_pDB = pNewConnection;

	// just in case (this could mess with multi-user databases)
	return TRUE;
}

static gboolean db_is_connected(void)
{
	// 'mysql_ping' will also attempt a re-connect if necessary
	return (g_pDB != NULL && mysql_ping(g_pDB->pMySQLConnection) == MYSQL_RESULT_SUCCESS);
}

// gets a descriptive string about the connection.  (do not free it.)
const gchar* db_get_connection_info()
{
	if(g_pDB == NULL || g_pDB->pMySQLConnection == NULL) {
		return "Not connected";
	}

	return mysql_get_host_info(g_pDB->pMySQLConnection);
}


/******************************************************
** database utility functions
******************************************************/

// call db_free_escaped_string() on returned string
gchar* db_make_escaped_string(const gchar* pszString)
{
	// make given string safe for inclusion in a SQL string
	if(!db_is_connected()) return g_strdup("");

	gint nLength = (strlen(pszString)*2) + 1;
	gchar* pszNew = g_malloc(nLength);
	mysql_real_escape_string(g_pDB->pMySQLConnection, pszNew, pszString, strlen(pszString));

	return pszNew; 		
}

void db_free_escaped_string(gchar* pszString)
{
	g_free(pszString);
}

/******************************************************
** data inserting
******************************************************/

static gboolean db_insert(const gchar* pszSQL, gint* pnReturnRowsInserted)
{
	g_assert(pszSQL != NULL);
	if(g_pDB == NULL) return FALSE;

	if(mysql_query(g_pDB->pMySQLConnection, pszSQL) != MYSQL_RESULT_SUCCESS) {
		//g_warning("db_query: %s (SQL: %s)\n", mysql_error(g_pDB->pMySQLConnection), pszSQL);
		return FALSE;
	}

	my_ulonglong uCount = mysql_affected_rows(g_pDB->pMySQLConnection);
	if(uCount > 0) {
		if(pnReturnRowsInserted != NULL) {
			*pnReturnRowsInserted = uCount;
		}
		return TRUE;
	}
	return FALSE;
}

gboolean db_insert_road(gint nLOD, gint nRoadNameID, gint nLayerType, gint nAddressLeftStart, gint nAddressLeftEnd, gint nAddressRightStart, gint nAddressRightEnd, gint nCityLeftID, gint nCityRightID, const gchar* pszZIPCodeLeft, const gchar* pszZIPCodeRight, GArray* pPointsArray, gint* pReturnID)
{
//	g_assert(pReturnID != NULL);
	if(!db_is_connected()) return FALSE;
	if(pPointsArray->len == 0) return TRUE; 	// skip 0-length

	gchar azCoordinateList[COORD_LIST_MAX] = {0};
	gint nCount = 0;
	gint i;
	for(i=0 ; i < pPointsArray->len ;i++) {
		mappoint_t* pPoint = &g_array_index(pPointsArray, mappoint_t, i);

		gchar azNewest[40];

		gchar azCoord1[20], azCoord2[20];
		if(nCount > 0) g_snprintf(azNewest, 40, ",%s %s", g_ascii_dtostr(azCoord1, 20, pPoint->fLatitude), g_ascii_dtostr(azCoord2, 20, pPoint->fLongitude));
		else g_snprintf(azNewest, 40, "%s %s", g_ascii_dtostr(azCoord1, 20, pPoint->fLatitude), g_ascii_dtostr(azCoord2, 20, pPoint->fLongitude));

		g_strlcat(azCoordinateList, azNewest, COORD_LIST_MAX);
		nCount++;
	}

	gchar* pszQuery;
	
	if(nLOD == 0) {
		pszQuery = g_strdup_printf(
			"INSERT INTO %s%d SET RoadNameID=%d, TypeID=%d, Coordinates=GeometryFromText('LINESTRING(%s)')"
			", AddressLeftStart=%d, AddressLeftEnd=%d, AddressRightStart=%d, AddressRightEnd=%d"
			", CityLeftID=%d, CityRightID=%d"
			", ZIPCodeLeft='%s', ZIPCodeRight='%s'",
			DB_ROADS_TABLENAME, nLOD, nRoadNameID, nLayerType, azCoordinateList,
			nAddressLeftStart, nAddressLeftEnd, nAddressRightStart, nAddressRightEnd,
			nCityLeftID, nCityRightID,
			pszZIPCodeLeft, pszZIPCodeRight);
	}
	else {
		pszQuery = g_strdup_printf(
			"INSERT INTO %s%d SET RoadNameID=%d, TypeID=%d, Coordinates=GeometryFromText('LINESTRING(%s)')",
			DB_ROADS_TABLENAME, nLOD, nRoadNameID, nLayerType, azCoordinateList);
	}

	mysql_query(g_pDB->pMySQLConnection, pszQuery);
	g_free(pszQuery);

	// return the new ID
	if(pReturnID != NULL) {
		*pReturnID = mysql_insert_id(g_pDB->pMySQLConnection);
	}
	return TRUE;
}

/******************************************************
**
******************************************************/

static gboolean db_roadname_get_id(const gchar* pszName, gint nSuffixID, gint* pnReturnID)
{
	gint nReturnID = 0;

	// create SQL for selecting RoadName.ID
	gchar* pszSafeName = db_make_escaped_string(pszName);
	gchar* pszSQL = g_strdup_printf("SELECT RoadName.ID FROM RoadName WHERE RoadName.Name='%s' AND RoadName.SuffixID=%d;", pszSafeName, nSuffixID);
	db_free_escaped_string(pszSafeName);

	// try query
	db_resultset_t* pResultSet = NULL;
	db_row_t aRow;
	db_query(pszSQL, &pResultSet);
	g_free(pszSQL);

	// get result?
	if(pResultSet) {
		if((aRow = db_fetch_row(pResultSet)) != NULL) {
			nReturnID = atoi(aRow[0]);
		}
		db_free_result(pResultSet);
	
		if(nReturnID != 0) {
			*pnReturnID = nReturnID;
			return TRUE;
		}
	}
	return FALSE;
}

gboolean db_insert_roadname(const gchar* pszName, gint nSuffixID, gint* pnReturnID)
{
	gint nRoadNameID = 0;

	// Step 1. Insert into RoadName
	if(db_roadname_get_id(pszName, nSuffixID, &nRoadNameID) == FALSE) {
		gchar* pszSafeName = db_make_escaped_string(pszName);
		gchar* pszSQL = g_strdup_printf("INSERT INTO RoadName SET Name='%s', SuffixID=%d", pszSafeName, nSuffixID);
		db_free_escaped_string(pszSafeName);

		if(db_insert(pszSQL, NULL)) {
			nRoadNameID = db_get_last_insert_id();
		}
		g_free(pszSQL);
	}
	
	if(nRoadNameID != 0) {
		if(pnReturnID != NULL) {
			*pnReturnID = nRoadNameID;
		}
		return TRUE;
	}
	return FALSE;
}

//
// insert / select city
//

// lookup numerical ID of a city by name
gboolean db_city_get_id(const gchar* pszName, gint nStateID, gint* pnReturnID)
{
	g_assert(pnReturnID != NULL);

	gint nReturnID = 0;
	// create SQL for selecting City.ID
	gchar* pszSafeName = db_make_escaped_string(pszName);
	gchar* pszSQL = g_strdup_printf("SELECT City.ID FROM City WHERE City.Name='%s' AND City.StateID=%d;", pszSafeName, nStateID);
	db_free_escaped_string(pszSafeName);

	// try query
	db_resultset_t* pResultSet = NULL;
	db_row_t aRow;
	db_query(pszSQL, &pResultSet);
	g_free(pszSQL);
	// get result?
	if(pResultSet) {
		if((aRow = db_fetch_row(pResultSet)) != NULL) {
			nReturnID = atoi(aRow[0]);
		}
		db_free_result(pResultSet);

		if(nReturnID != 0) {
			*pnReturnID = nReturnID;
			return TRUE;
		}
	}
	return FALSE;
}

gboolean db_city_get_name(gint nCityID, gchar** ppszReturnName)
{
	g_assert(ppszReturnName != NULL);

	// create SQL
	gchar* pszSQL = g_strdup_printf("SELECT City.Name FROM City WHERE ID='%d';", nCityID);

	// try query
	db_resultset_t* pResultSet = NULL;
	db_row_t aRow;
	db_query(pszSQL, &pResultSet);
	g_free(pszSQL);
	// get result?
	if(pResultSet) {
		if((aRow = db_fetch_row(pResultSet)) != NULL) {
			*ppszReturnName = g_strdup(aRow[0]);
		}
		db_free_result(pResultSet);
		return TRUE;
	}
	return FALSE;
}

gboolean db_insert_city(const gchar* pszName, gint nStateID, gint* pnReturnCityID)
{
	gint nCityID = 0;

	// Step 1. Insert into RoadName
	if(db_city_get_id(pszName, nStateID, &nCityID) == FALSE) {
		gchar* pszSafeName = db_make_escaped_string(pszName);
		gchar* pszSQL = g_strdup_printf("INSERT INTO City SET Name='%s', StateID=%d", pszSafeName, nStateID);
		db_free_escaped_string(pszSafeName);

		if(db_insert(pszSQL, NULL)) {
			*pnReturnCityID = db_get_last_insert_id();
		}
		g_free(pszSQL);
	}
	else {
		// already exists, use the existing one.
		*pnReturnCityID = nCityID;
	}
	return TRUE;
}

//
// insert / select state
//
// lookup numerical ID of a state by name
gboolean db_state_get_id(const gchar* pszName, gint* pnReturnID)
{
	gint nReturnID = 0;

	// create SQL for selecting City.ID
	gchar* pszSafeName = db_make_escaped_string(pszName);
	gchar* pszSQL = g_strdup_printf("SELECT State.ID FROM State WHERE State.Name='%s' OR State.Code='%s';", pszSafeName, pszSafeName);
	db_free_escaped_string(pszSafeName);

	// try query
	db_resultset_t* pResultSet = NULL;
	db_row_t aRow;
	db_query(pszSQL, &pResultSet);
	g_free(pszSQL);
	// get result?
	if(pResultSet) {
		if((aRow = db_fetch_row(pResultSet)) != NULL) {
			nReturnID = atoi(aRow[0]);
		}
		db_free_result(pResultSet);

		if(nReturnID != 0) {
			*pnReturnID = nReturnID;
			return TRUE;
		}
	}
	return FALSE;
}

gboolean db_insert_state(const gchar* pszName, const gchar* pszCode, gint nCountryID, gint* pnReturnStateID)
{
	gint nStateID = 0;

	// Step 1. Insert into RoadName
	if(db_state_get_id(pszName, &nStateID) == FALSE) {
		gchar* pszSafeName = db_make_escaped_string(pszName);
		gchar* pszSafeCode = db_make_escaped_string(pszCode);
		gchar* pszSQL = g_strdup_printf("INSERT INTO State SET Name='%s', Code='%s', CountryID=%d", pszSafeName, pszSafeCode, nCountryID);
		db_free_escaped_string(pszSafeName);
		db_free_escaped_string(pszSafeCode);

		if(db_insert(pszSQL, NULL)) {
			*pnReturnStateID = db_get_last_insert_id();
		}
		g_free(pszSQL);
	}
	else {
		// already exists, use the existing one.
		*pnReturnStateID = nStateID;
	}
	return TRUE;
}

#define WKB_POINT                  1	// only two we care about
#define WKB_LINESTRING             2

void db_parse_wkb_point(const gint8* data, mappoint_t* pPoint)
{
	g_assert(sizeof(double) == 8);	// mysql gives us 8 bytes per point

	gint nByteOrder = *data++;	// first byte tells us the byte order
	g_assert(nByteOrder == 1);

	gint nGeometryType = *((gint32*)data);
	data += sizeof(gint32);
	g_assert(nGeometryType == WKB_POINT);

	pPoint->fLatitude = *((double*)data);
	data += sizeof(double);
	pPoint->fLongitude = *((double*)data);
	data += sizeof(double);
}

void db_parse_wkb_linestring(const gint8* data, GArray* pMapPointsArray, maprect_t* pBoundingRect)
{
	g_assert(sizeof(double) == 8);	// mysql gives us 8 bytes per point

	gint nByteOrder = *data++;	// first byte tells us the byte order
	g_assert(nByteOrder == 1);

	gint nGeometryType = *((gint32*)data);
	data += sizeof(gint32);
	g_assert(nGeometryType == WKB_LINESTRING);

	gint nNumPoints = *((gint32*)data);	// NOTE for later: this field doesn't exist for type POINT
	data += sizeof(gint32);

    g_array_set_size(pMapPointsArray, nNumPoints);

	pBoundingRect->A.fLatitude = MAX_LATITUDE;
	pBoundingRect->A.fLongitude = MAX_LONGITUDE;
	pBoundingRect->B.fLatitude = MIN_LATITUDE;
	pBoundingRect->B.fLongitude = MIN_LONGITUDE;

	gint i = 0;
	while(nNumPoints > 0) {
		mappoint_t* p = &g_array_index(pMapPointsArray, mappoint_t, i);

		p->fLatitude = *((double*)data);
		pBoundingRect->A.fLatitude = min(pBoundingRect->A.fLatitude, p->fLatitude);
		pBoundingRect->B.fLatitude = max(pBoundingRect->B.fLatitude, p->fLatitude);
		data += sizeof(double);

		p->fLongitude = *((double*)data);
		pBoundingRect->A.fLongitude = min(pBoundingRect->A.fLongitude, p->fLongitude);
		pBoundingRect->B.fLongitude = max(pBoundingRect->B.fLongitude, p->fLongitude);
		data += sizeof(double);

		nNumPoints--;
		i++;
	}
}

void db_create_tables()
{
	db_query("CREATE DATABASE IF NOT EXISTS roadster;", NULL);
	db_query("USE roadster;", NULL);

	// For development: run these once to update your tables
//	db_query("ALTER TABLE RoadName ADD COLUMN NameSoundex CHAR(4) NOT NULL;", NULL);
//	db_query("UPDATE RoadName SET NameSoundex=SOUNDEX(Name);", NULL);
//	db_query("ALTER TABLE RoadName ADD INDEX (NameSoundex);", NULL);

	// Road
	db_query(
		"CREATE TABLE IF NOT EXISTS Road0("
//		" ID INT4 UNSIGNED NOT NULL AUTO_INCREMENT,"	// XXX: can we get away with INT3 ?
		" TypeID INT1 UNSIGNED NOT NULL,"
		" RoadNameID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes
		" AddressLeftStart INT2 UNSIGNED NOT NULL,"
		" AddressLeftEnd INT2 UNSIGNED NOT NULL,"
		" AddressRightStart INT2 UNSIGNED NOT NULL,"
		" AddressRightEnd INT2 UNSIGNED NOT NULL,"
		" CityLeftID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes
		" CityRightID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes
		" ZIPCodeLeft CHAR(6) NOT NULL,"
		" ZIPCodeRight CHAR(6) NOT NULL,"
		" Coordinates point NOT NULL,"

	    // lots of indexes:
//		" PRIMARY KEY (ID),"	// XXX: we'll probably want to keep a unique ID, but we don't use this for anything yet.
		" INDEX(RoadNameID),"	// to get roads when we've matched a RoadName
		" SPATIAL KEY (Coordinates));", NULL);

	db_query(
		"CREATE TABLE IF NOT EXISTS Road1("
		" TypeID INT1 UNSIGNED NOT NULL,"
		" RoadNameID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes
		" Coordinates point NOT NULL,"
		" SPATIAL KEY (Coordinates));", NULL);

	db_query(
		"CREATE TABLE IF NOT EXISTS Road2("
		" TypeID INT1 UNSIGNED NOT NULL,"
		" RoadNameID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes
		" Coordinates point NOT NULL,"
		" SPATIAL KEY (Coordinates));", NULL);
	
	db_query(
		"CREATE TABLE IF NOT EXISTS Road3("
		" TypeID INT1 UNSIGNED NOT NULL,"
		" RoadNameID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes
		" Coordinates point NOT NULL,"
		" SPATIAL KEY (Coordinates));", NULL);

	// RoadName
	db_query(
		"CREATE TABLE IF NOT EXISTS RoadName("
		" ID INT3 UNSIGNED NOT NULL auto_increment,"	// NOTE: 3 bytes
		" Name VARCHAR(30) NOT NULL,"
		" NameSoundex CHAR(10) NOT NULL,"	// see soundex() function
		" SuffixID INT1 UNSIGNED NOT NULL,"
		" PRIMARY KEY (ID),"				// for joining RoadName to Road 
		" INDEX (Name(7)));"					// for searching by RoadName. 7 is enough for decent uniqueness(?)
//		" INDEX (NameSoundex));"			// Nicer way to search by RoadName(?)
		,NULL);

	// City
	db_query(
		"CREATE TABLE IF NOT EXISTS City("
		" ID INT3 UNSIGNED NOT NULL AUTO_INCREMENT,"	// NOTE: 3 bytes
		" StateID INT2 UNSIGNED NOT NULL,"		// NOTE: 2 bytes
		" Name CHAR(60) NOT NULL,"			// are city names ever 60 chars anyway??  TIGER think so
		" PRIMARY KEY (ID),"
		" INDEX (StateID),"				// for finding all cities by state (needed?)
		" INDEX (Name(6)));"				// 6 is enough for decent uniqueness.
	    ,NULL);

	// State
	db_query(
		"CREATE TABLE IF NOT EXISTS State("
		" ID INT2 UNSIGNED NOT NULL AUTO_INCREMENT,"	// NOTE: 2 bytes (enough to go global..?)
		" Name CHAR(40) NOT NULL,"
		" Code CHAR(3) NOT NULL,"			// eg. "MA"
		" CountryID INT2 NOT NULL,"			// NOTE: 2 bytes
		" PRIMARY KEY (ID),"
		" INDEX (Name(5)));"				// 5 is enough for decent uniqueness.
	    ,NULL);

	// Location
	db_query(
		"CREATE TABLE IF NOT EXISTS Location("
		" ID INT4 UNSIGNED NOT NULL AUTO_INCREMENT,"
		" LocationSetID INT3 NOT NULL,"				// NOTE: 3 bytes
		" Coordinates point NOT NULL,"
		" PRIMARY KEY (ID),"
		" INDEX(LocationSetID),"
		" SPATIAL KEY (Coordinates));", NULL);

	// Location Attribute Name
	db_query("CREATE TABLE IF NOT EXISTS LocationAttributeName("
		" ID INT3 UNSIGNED NOT NULL AUTO_INCREMENT,"		// NOTE: 3 bytes. (16 million possibilities)
		" Name VARCHAR(30) NOT NULL,"
		" PRIMARY KEY (ID),"
		" UNIQUE INDEX (Name));", NULL);

	// Location Attribute Value
	db_query("CREATE TABLE IF NOT EXISTS LocationAttributeValue("
		// a unique ID for the value
		" ID INT4 UNSIGNED NOT NULL AUTO_INCREMENT,"
		// which location this value applies to
		" LocationID INT4 UNSIGNED NOT NULL,"
		// type 'name' of this name=value pair
		" AttributeNameID INT3 UNSIGNED NOT NULL,"		// NOTE: 3 bytes.
		// the actual value, a text blob
		" Value TEXT NOT NULL,"
		" PRIMARY KEY (ID),"			// for fast updates/deletes (needed only if POIs can have multiple values per name, otherwise LocationID_AttributeID is unique)
		" INDEX (LocationID, AttributeNameID)," // for searching values for a given POI
		" FULLTEXT(Value));", NULL);		// for sexy fulltext searching of values!

	// Location Set
	db_query("CREATE TABLE IF NOT EXISTS LocationSet("
		" ID INT3 UNSIGNED NOT NULL AUTO_INCREMENT,"		// NOTE: 3 bytes.	(would 2 be enough?)
		" Name VARCHAR(60) NOT NULL,"
		" IconName VARCHAR(60) NOT NULL,"
		" PRIMARY KEY (ID));", NULL);

//     // Remote File Cache
//     db_query("CREATE TABLE IF NOT EXISTS RemoteFileCache("
//         " ID INT3 UNSIGNED NOT NULL AUTO_INCREMENT,"        // NOTE: 3 bytes.
//         " RemoteFilePath VARCHAR(255) NOT NULL,"            // the full URI (eg. "http://site/path/file.png"
//         " LocalFileName VARCHAR(255) NOT NULL,"             // just the 'name' part.  the path should be prepended
//         " LocalFileSize INT4 NOT NULL,"
//         " PRIMARY KEY (ID),"
//         " INDEX (RemoteFilePath),"
//         " INDEX (LocalFileName))",
//         NULL);
}

#ifdef ROADSTER_DEAD_CODE
// static guint db_count_table_rows(const gchar* pszTable)
// {
//     if(!db_is_connected()) return 0;
//
//     MYSQL_RES* pResultSet;
//     MYSQL_ROW aRow;
//     gchar azQuery[MAX_SQLBUFFER_LEN];
//     guint uRows = 0;
//
//     // count rows
//     g_snprintf(azQuery, MAX_SQLBUFFER_LEN, "SELECT COUNT(*) FROM %s;", pszTable);
//     if(mysql_query(g_pDB->pMySQLConnection, azQuery) != MYSQL_RESULT_SUCCESS) {
//         g_message("db_count_table_rows query failed: %s\n", mysql_error(g_pDB->pMySQLConnection));
//         return 0;
//     }
//     if((pResultSet = MYSQL_GET_RESULT(g_pDB->pMySQLConnection)) != NULL) {
//         if((aRow = mysql_fetch_row(pResultSet)) != NULL) {
//             uRows = atoi(aRow[0]);
//         }
//         mysql_free_result(pResultSet);
//     }
//     return uRows;
// }
#endif
