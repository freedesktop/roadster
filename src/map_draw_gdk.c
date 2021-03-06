/***************************************************************************
 *            map_draw_gdk.c
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

#define MAX_GDK_LINE_SEGMENTS (2000)

//#define ENABLE_MAP_GRAYSCALE_HACK 	// just a little test.  black and white might be good for something
//#define ENABLE_CLIPPER_SHRINK_RECT_TEST	// NOTE: even with this on, objects won't be clipped unless they cross the real screen border
//#define ENABLE_RANDOM_ROAD_COLORS

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <math.h>

#include "main.h"
#include "gui.h"
#include "map.h"
#include "mainwindow.h"
#include "util.h"
#include "db.h"
#include "road.h"
#include "map_style.h"
#include "map_math.h"
#include "locationset.h"
#include "location.h"
#include "scenemanager.h"

//static void map_draw_gdk_background(map_t* pMap, GdkPixmap* pPixmap);
static void map_draw_gdk_layer_polygons(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, GPtrArray* pRoadsArray, maplayerstyle_t* pLayerStyle);
static void map_draw_gdk_layer_lines(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, GPtrArray* pRoadsArray, maplayerstyle_t* pLayerStyle);
static void map_draw_gdk_layer_fill(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, maplayerstyle_t* pLayerStyle);

//static void map_draw_gdk_locations(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics);
//static void map_draw_gdk_locationset(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, locationset_t* pLocationSet, GPtrArray* pLocationsArray);

typedef struct {
	GdkPixmap* pPixmap;
	GdkGC* pGC;
	maplayerstyle_t* pLayerStyle;
	rendermetrics_t* pRenderMetrics;
} gdk_draw_context_t;

//static void map_draw_gdk_tracks(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics);

void map_draw_gdk_set_color(GdkGC* pGC, color_t* pColor)
{
	GdkColor clr;

#ifdef ENABLE_MAP_GRAYSCALE_HACK
	clr.red = clr.green = clr.blue = ((pColor->fRed + pColor->fGreen + pColor->fBlue) / 3.0) * 65535;
#else
	clr.red = pColor->fRed * 65535;
	clr.green = pColor->fGreen * 65535;
	clr.blue = pColor->fBlue * 65535;
#endif
	gdk_gc_set_rgb_fg_color(pGC, &clr);
}

void map_draw_gdk_xor_rect(map_t* pMap, GdkDrawable* pTargetDrawable, screenrect_t* pRect)
{
	GdkGC* pGC = pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)];

	GdkGCValues gcValues;
	gdk_gc_get_values(pGC, &gcValues);

	GdkColor clr = {0, 65535, 65535, 65535};
	gdk_gc_set_function(pGC, GDK_XOR);
	gdk_gc_set_rgb_fg_color(pGC, &clr);
	gdk_draw_rectangle(pTargetDrawable, pGC, FALSE, 
					   min(pRect->A.nX, pRect->B.nX), min(pRect->A.nY, pRect->B.nY),	// x,y
					   map_screenrect_width(pRect), map_screenrect_height(pRect));		// w,h

	gdk_gc_set_values(pGC, &gcValues, GDK_GC_FUNCTION | GDK_GC_FOREGROUND);
}

void map_draw_gdk(map_t* pMap, GPtrArray* pTiles, rendermetrics_t* pRenderMetrics, GdkPixmap* pPixmap, gint nDrawFlags)
{
	TIMER_BEGIN(maptimer, "BEGIN RENDER MAP (gdk)");

	GdkGC* pGC = pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)];

	// 1. Save values (so we can restore them)
	GdkGCValues gcValues;
	gdk_gc_get_values(pGC, &gcValues);

	// 2. Drawing
	if(nDrawFlags & DRAWFLAG_GEOMETRY) {
		gint i;

		gint nStyleZoomLevel = g_sZoomLevels[pRenderMetrics->nZoomLevel-1].nStyleZoomLevel;

		// 2.2. Draw layer list in reverse order (painter's algorithm: http://en.wikipedia.org/wiki/Painter's_algorithm )
		for(i=pMap->pLayersArray->len-1 ; i>=0 ; i--) {
			maplayer_t* pLayer = g_ptr_array_index(pMap->pLayersArray, i);

			if(pLayer->nDrawType == MAP_LAYER_RENDERTYPE_FILL) {
				map_draw_gdk_layer_fill(pMap, pPixmap,  pRenderMetrics,
										 pLayer->paStylesAtZoomLevels[nStyleZoomLevel-1]);       // style
			}
			else if(pLayer->nDrawType == MAP_LAYER_RENDERTYPE_LINES) {
				gint iTile;
				for(iTile=0 ; iTile < pTiles->len ; iTile++) {
					maptile_t* pTile = g_ptr_array_index(pTiles, iTile);
					map_draw_gdk_layer_lines(pMap, pPixmap, pRenderMetrics,
											 pTile->apMapObjectArrays[pLayer->nDataSource],               // data
											 pLayer->paStylesAtZoomLevels[nStyleZoomLevel-1]);       // style
				}
			}
			else if(pLayer->nDrawType == MAP_LAYER_RENDERTYPE_POLYGONS) {
				gint iTile;
				for(iTile=0 ; iTile < pTiles->len ; iTile++) {
					maptile_t* pTile = g_ptr_array_index(pTiles, iTile);
					map_draw_gdk_layer_polygons(pMap, pPixmap, pRenderMetrics,
												pTile->apMapObjectArrays[pLayer->nDataSource],          // data
												pLayer->paStylesAtZoomLevels[nStyleZoomLevel-1]);    // style
				}
			}
			else if(pLayer->nDrawType == MAP_LAYER_RENDERTYPE_LOCATIONS) {
//                 map_draw_gdk_locations(pMap, pPixmap, pRenderMetrics);
			}
			else {
//                 g_print("pLayer->nDrawType = %d\n", pLayer->nDrawType);
//                 g_assert_not_reached();
			}
		}
	}

	// 3. Labels
	// ...GDK just shouldn't attempt text. :)

	// 4. Restore values
	gdk_gc_set_values(pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)], &gcValues, GDK_GC_FOREGROUND | GDK_GC_BACKGROUND | GDK_GC_LINE_WIDTH | GDK_GC_LINE_STYLE | GDK_GC_CAP_STYLE | GDK_GC_JOIN_STYLE);
	TIMER_END(maptimer, "END RENDER MAP (gdk)");
}

// useful for filling the screen with a color.  not much else.
static void map_draw_gdk_layer_fill(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, maplayerstyle_t* pLayerStyle)
{
	GdkGC* pGC = pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)];

	GdkGCValues gcValues;
	if(pLayerStyle->pGlyphFill != NULL) {
		// Instead of filling with a color, fill with a tiled image
		gdk_gc_get_values(pGC, &gcValues);
		gdk_gc_set_fill(pGC, GDK_TILED);
		gdk_gc_set_tile(pGC, glyph_get_pixmap(pLayerStyle->pGlyphFill, pMap->pTargetWidget));
		
		// This makes the fill image scroll with the map, instead of staying still
		gdk_gc_set_ts_origin(pGC, SCALE_X(pRenderMetrics, pRenderMetrics->fScreenLongitude), SCALE_Y(pRenderMetrics, pRenderMetrics->fScreenLatitude));
	}
	else {
		// Simple color fill
		map_draw_gdk_set_color(pGC, &(pLayerStyle->clrPrimary));
	}

	gdk_draw_rectangle(pPixmap, pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)],
			TRUE, 0,0, pMap->MapDimensions.uWidth, pMap->MapDimensions.uHeight);

	if(pLayerStyle->pGlyphFill != NULL) {
		// Restore fill style
		gdk_gc_set_values(pGC, &gcValues, GDK_GC_FILL);
	}
}

// 
static void map_draw_gdk_polygons(const GArray* pMapPointsArray, const gdk_draw_context_t* pContext)
{
	// Copy all points into this array.  Yuuup this is slow. :)
	GdkPoint aPoints[MAX_GDK_LINE_SEGMENTS];
	mappoint_t* pPoint;

	gint iPoint;
	for(iPoint=0 ; iPoint<pMapPointsArray->len ; iPoint++) {
		pPoint = &g_array_index(pMapPointsArray, mappoint_t, iPoint);

		aPoints[iPoint].x = pContext->pLayerStyle->nPixelOffsetX + (gint)SCALE_X(pContext->pRenderMetrics, pPoint->fLongitude);
		aPoints[iPoint].y = pContext->pLayerStyle->nPixelOffsetY + (gint)SCALE_Y(pContext->pRenderMetrics, pPoint->fLatitude);
	}

	gdk_draw_polygon(pContext->pPixmap, pContext->pGC, TRUE, aPoints, pMapPointsArray->len);
}

static void map_draw_gdk_layer_polygons(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, GPtrArray* pRoadsArray, maplayerstyle_t* pLayerStyle)
{
	road_t* pRoad;

	if(pLayerStyle->clrPrimary.fAlpha == 0.0) return;	// invisible?  (not that we respect it in gdk drawing anyway)
	if(pRoadsArray->len == 0) return;

	GdkGC* pGC = pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)];

	GdkGCValues gcValues;
	if(pLayerStyle->pGlyphFill != NULL) {
		// Instead of filling with a color, fill with a tiled image
		gdk_gc_get_values(pGC, &gcValues);
		gdk_gc_set_fill(pGC, GDK_TILED);
		gdk_gc_set_tile(pGC, glyph_get_pixmap(pLayerStyle->pGlyphFill, pMap->pTargetWidget));
		
		// This makes the fill image scroll with the map, instead of staying still
		gdk_gc_set_ts_origin(pGC, SCALE_X(pRenderMetrics, pRenderMetrics->fScreenLongitude), SCALE_Y(pRenderMetrics, pRenderMetrics->fScreenLatitude));
	}
	else {
		// Simple color fill
		map_draw_gdk_set_color(pGC, &(pLayerStyle->clrPrimary));
	}

	gdk_draw_context_t context;
	context.pPixmap = pPixmap;
	context.pGC = pGC;
	context.pLayerStyle = pLayerStyle;
	context.pRenderMetrics = pRenderMetrics;

	gint iString;
	for(iString=0 ; iString<pRoadsArray->len ; iString++) {
		pRoad = g_ptr_array_index(pRoadsArray, iString);

		EOverlapType eOverlapType = map_rect_a_overlap_type_with_rect_b(&(pRoad->rWorldBoundingBox), &(pRenderMetrics->rWorldBoundingBox));
		if(eOverlapType == OVERLAP_NONE) {
			continue;
		}

		// XXX: should we remove this?
		if(pRoad->pMapPointsArray->len < 3) {
			//g_warning("not drawing polygon with < 3 points\n");
			continue;
		}

		if(pRoad->pMapPointsArray->len > MAX_GDK_LINE_SEGMENTS) {
			//g_warning("not drawing polygon with > %d points\n", MAX_GDK_LINE_SEGMENTS);
			continue;
		}

		if(eOverlapType == OVERLAP_PARTIAL) {
			// draw clipped
       		GArray* pClipped = g_array_sized_new(FALSE, FALSE, sizeof(mappoint_t), pRoad->pMapPointsArray->len + 20);	// it's rarely more than a few extra points
			map_math_clip_pointstring_to_worldrect(pRoad->pMapPointsArray, &(pRenderMetrics->rWorldBoundingBox), pClipped);
			map_draw_gdk_polygons(pClipped, &context);
			g_array_free(pClipped, TRUE);
		}
		else {
			// draw normally
			map_draw_gdk_polygons(pRoad->pMapPointsArray, &context);
		}
	}
	if(pLayerStyle->pGlyphFill != NULL) {
		// Restore fill style
		gdk_gc_set_values(pGC, &gcValues, GDK_GC_FILL);
	}
}

static void map_draw_gdk_lines(const GArray* pMapPointsArray, const gdk_draw_context_t* pContext)
{
	// Copy all points into this array.  Yuuup this is slow. :)
	GdkPoint aPoints[MAX_GDK_LINE_SEGMENTS];
	mappoint_t* pPoint;

	gint iPoint;
	for(iPoint=0 ; iPoint<pMapPointsArray->len ; iPoint++) {
		pPoint = &g_array_index(pMapPointsArray, mappoint_t, iPoint);

		aPoints[iPoint].x = pContext->pLayerStyle->nPixelOffsetX + (gint)SCALE_X(pContext->pRenderMetrics, pPoint->fLongitude);
		aPoints[iPoint].y = pContext->pLayerStyle->nPixelOffsetY + (gint)SCALE_Y(pContext->pRenderMetrics, pPoint->fLatitude);
	}
	
	gdk_draw_lines(pContext->pPixmap, pContext->pGC, aPoints, pMapPointsArray->len);
}

static void map_draw_gdk_layer_lines(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, GPtrArray* pRoadsArray, maplayerstyle_t* pLayerStyle)
{
	road_t* pRoad;
	gint iString;

	if(pLayerStyle->fLineWidth <= 0.0) return;			// Don't draw invisible lines
	if(pLayerStyle->clrPrimary.fAlpha == 0.0) return;	// invisible?  (not that we respect it in gdk drawing anyway)

	GdkGC* pGC = pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)];

	// Translate generic cap style into GDK constant
	gint nCapStyle;
	if(pLayerStyle->nCapStyle == MAP_CAP_STYLE_ROUND) {
		nCapStyle = GDK_CAP_ROUND;
	}
	else {
		nCapStyle = GDK_CAP_PROJECTING;
	}

	// Convert to integer width.  Ouch!
	gint nLineWidth = (gint)(pLayerStyle->fLineWidth);

	// Use GDK dash style if ANY dash pattern is set
	gint nDashStyle = GDK_LINE_SOLID;
	if(pLayerStyle->pDashStyle != NULL) {
		gdk_gc_set_dashes(pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)],
				0, /* offset to start at */
				pLayerStyle->pDashStyle->panDashList,
				pLayerStyle->pDashStyle->nDashCount);
		
		nDashStyle = GDK_LINE_ON_OFF_DASH;
		// further set line attributes below...
	}

	// Set line style
	gdk_gc_set_line_attributes(pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)],
			   nLineWidth, nDashStyle, nCapStyle, GDK_JOIN_ROUND);

	map_draw_gdk_set_color(pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)], &(pLayerStyle->clrPrimary));

	gdk_draw_context_t context;
	context.pPixmap = pPixmap;
	context.pGC = pGC;
	context.pLayerStyle = pLayerStyle;
	context.pRenderMetrics = pRenderMetrics;

	for(iString=0 ; iString<pRoadsArray->len ; iString++) {
		pRoad = g_ptr_array_index(pRoadsArray, iString);

		EOverlapType eOverlapType = map_rect_a_overlap_type_with_rect_b(&(pRoad->rWorldBoundingBox), &(pRenderMetrics->rWorldBoundingBox));
		if(eOverlapType == OVERLAP_NONE) {
			continue;
		}

		if(pRoad->pMapPointsArray->len > MAX_GDK_LINE_SEGMENTS) {
			//g_warning("not drawing line with > %d points\n", MAX_GDK_LINE_SEGMENTS);
			continue;
		}

		if(pRoad->pMapPointsArray->len < 2) {
			//g_warning("not drawing line with < 2 points\n");
			continue;
		}

#ifdef ENABLE_RANDOM_ROAD_COLORS
		color_t clr;
		util_random_color(&clr);
		map_draw_gdk_set_color(pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)], &clr);
#endif
		if(eOverlapType == OVERLAP_PARTIAL) {
			// TODO: draw clipped
			map_draw_gdk_lines(pRoad->pMapPointsArray, &context);
		}
		else {
			// draw directly
			map_draw_gdk_lines(pRoad->pMapPointsArray, &context);
		}
	}
}

// Draw all locations from sets marked visible
#ifdef ROADSTER_DEAD_CODE
/*
static void map_draw_gdk_locations(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics)
{
	const GPtrArray* pLocationSetsArray = locationset_get_array();

	gint i;
	for(i=0 ; i<pLocationSetsArray->len ; i++) {
		locationset_t* pLocationSet = g_ptr_array_index(pLocationSetsArray, i);
		if(!locationset_is_visible(pLocationSet)) continue;

		// 2. Get the array of Locations from the hash table using LocationSetID
		GPtrArray* pLocationsArray;
		pLocationsArray = g_hash_table_lookup(pMap->pLocationArrayHashTable, &(pLocationSet->nID));
		if(pLocationsArray != NULL) {
			// found existing array
			map_draw_gdk_locationset(pMap, pPixmap, pRenderMetrics, pLocationSet, pLocationsArray);
		}
		else {
			// none to draw
		}
	}
}

static void map_draw_gdk_locationset(map_t* pMap, GdkPixmap* pPixmap, rendermetrics_t* pRenderMetrics, locationset_t* pLocationSet, GPtrArray* pLocationsArray)
{
	//g_print("Drawing set with %d\n", pLocationsArray->len);
	gint i;
	for(i=0 ; i<pLocationsArray->len ; i++) {
		location_t* pLocation = g_ptr_array_index(pLocationsArray, i);

		// bounding box test
		if(pLocation->Coordinates.fLatitude < pRenderMetrics->rWorldBoundingBox.A.fLatitude
		   || pLocation->Coordinates.fLongitude < pRenderMetrics->rWorldBoundingBox.A.fLongitude
		   || pLocation->Coordinates.fLatitude > pRenderMetrics->rWorldBoundingBox.B.fLatitude
		   || pLocation->Coordinates.fLongitude > pRenderMetrics->rWorldBoundingBox.B.fLongitude)
		{
		    continue;   // not visible
		}

		gdouble fX = SCALE_X(pRenderMetrics, pLocation->Coordinates.fLongitude);
		gdouble fY = SCALE_Y(pRenderMetrics, pLocation->Coordinates.fLatitude);

		GdkGC* pGC = pMap->pTargetWidget->style->fg_gc[GTK_WIDGET_STATE(pMap->pTargetWidget)];
		if(map_get_zoomlevel(pMap) <= 3) {
			glyph_draw_centered(pLocationSet->pMapGlyphSmall, pPixmap, pGC, fX, fY);
		}
		else {
			glyph_draw_centered(pLocationSet->pMapGlyph, pPixmap, pGC, fX, fY);
		}
	}
}
*/
#endif
