/***************************************************************************
 *            mainwindow.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <gtk/gtksignal.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <cairo.h>
#include <cairo-xlib.h>

#include "main.h"
#include "search_road.h"
#include "gui.h"
#include "util.h"
#include "gotowindow.h"
#include "db.h"
#include "map.h"
#include "map_style.h"
#include "importwindow.h"
#include "welcomewindow.h"
#include "locationset.h"
#include "gpsclient.h"
#include "mainwindow.h"
#include "glyph.h"
#include "animator.h"
#include "history.h"
#include "tooltip.h"

#define PROGRAM_NAME			"Roadster"
#define PROGRAM_COPYRIGHT		"Copyright (c) 2005 Ian McIntosh"
#define PROGRAM_DESCRIPTION		"Mapping for everyone!"
#define WEBSITE_URL				"http://linuxadvocate.org/projects/roadster"

#define MAP_STYLE_FILENAME 		("layers.xml")

// how long after stopping various movements should we redraw in high-quality mode
#define DRAW_PRETTY_SCROLL_TIMEOUT_MS	(110)	// NOTE: should be longer than the SCROLL_TIMEOUT_MS below!!
#define DRAW_PRETTY_ZOOM_TIMEOUT_MS	(180)
#define DRAW_PRETTY_DRAG_TIMEOUT_MS	(250)
#define DRAW_PRETTY_RESIZE_TIMEOUT_MS	(180)

#define SCROLL_TIMEOUT_MS				(70)		// how often (in MS) to move
#define SCROLL_DISTANCE_IN_PIXELS		(60)		// how far to move every (above) MS
#define BORDER_SCROLL_CLICK_TARGET_SIZE	(16)		// the size of the click target (distance from edge of map view) to begin scrolling

#define SLIDE_TIMEOUT_MS			(50)	// time between frames (in MS) for smooth-sliding on double click
#define	SLIDE_TIME_IN_SECONDS		(0.4)	// how long the whole slide should take, in seconds
#define	SLIDE_TIME_IN_SECONDS_AUTO	(0.8)	// time for sliding to search results, etc.

// Layerlist columns
#define LAYERLIST_COLUMN_ENABLED	(0)
#define LAYERLIST_COLUMN_NAME		(1)

// Locationset columns
#define	LOCATIONSETLIST_COLUMN_ENABLED  (0)
#define LOCATIONSETLIST_COLUMN_NAME		(1)
#define LOCATIONSETLIST_COLUMN_ID		(2)

// 
#define MOUSE_BUTTON_LEFT				(1)
#define MOUSE_BUTTON_RIGHT				(3)

// Limits
#define MAX_SEARCH_TEXT_LENGTH			(100)
#define SPEED_LABEL_FORMAT				("<span font_desc='32'>%.0f</span>")

#define TOOLTIP_FORMAT					("%s")

#define	ROAD_TOOLTIP_BG_COLOR			65535, 65535, 39000
#define	LOCATION_TOOLTIP_BG_COLOR		52800, 60395, 65535

// Settings
#define TIMER_GPS_REDRAW_INTERVAL_MS	(2500)		// lower this (to 1000?) when it's faster to redraw track

#define	TOOLTIP_OFFSET_X (20)
#define	TOOLTIP_OFFSET_Y (0)

#define MAX_DISTANCE_FOR_AUTO_SLIDE_IN_PIXELS	(3500.0)	// when selecting search results, we slide to them instead of jumping if they are within this distance

// Types
typedef struct {
	GdkCursorType m_CursorType;
	GdkCursor* m_pGdkCursor;
} cursor_t;

typedef struct {
	char* m_szName;
	cursor_t m_Cursor;
} toolsettings_t;

typedef enum {
	kToolPointer = 0,
	kToolZoom = 1,
} EToolType;

// Prototypes
static gboolean mainwindow_on_mouse_button_click(GtkWidget* w, GdkEventButton *event);
static gboolean mainwindow_on_mouse_motion(GtkWidget* w, GdkEventMotion *event);
static gboolean mainwindow_on_mouse_scroll(GtkWidget* w, GdkEventScroll *event);
static gboolean mainwindow_on_expose_event(GtkWidget *pDrawingArea, GdkEventExpose *event, gpointer data);
static gint mainwindow_on_configure_event(GtkWidget *pDrawingArea, GdkEventConfigure *event);
static gboolean mainwindow_callback_on_gps_redraw_timeout(gpointer pData);
static gboolean mainwindow_callback_on_slide_timeout(gpointer pData);
static void mainwindow_setup_selected_tool(void);
static gboolean mainwindow_on_enter_notify(GtkWidget* w, GdkEventCrossing *event);
static gboolean mainwindow_on_leave_notify(GtkWidget* w, GdkEventCrossing *event);
static void mainwindow_on_locationset_visible_checkbox_clicked(GtkCellRendererToggle *cell, gchar *path_str, gpointer data);

void mainwindow_add_history();

void mainwindow_map_center_on_mappoint(mappoint_t* pPoint);
void mainwindow_map_center_on_windowpoint(gint nX, gint nY);

void mainwindow_on_zoomscale_value_changed(GtkRange *range, gpointer user_data);

struct {
	gint m_nX;
	gint m_nY;
} g_aDirectionMultipliers[] = {{0,0}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}};

struct {
	gint m_nCursor;
} g_aDirectionCursors[] = {GDK_LEFT_PTR, GDK_TOP_SIDE, GDK_TOP_RIGHT_CORNER, GDK_RIGHT_SIDE, GDK_BOTTOM_RIGHT_CORNER, GDK_BOTTOM_SIDE, GDK_BOTTOM_LEFT_CORNER, GDK_LEFT_SIDE, GDK_TOP_LEFT_CORNER};

struct {
	GtkWindow* m_pWindow;
	GtkTooltips* m_pTooltips;
	GtkMenu* m_pMapPopupMenu;

	// Toolbar
	GtkToolbar* m_pToolbar;
//	GtkToolButton* m_pPointerToolButton;
//	GtkToolButton* m_pZoomToolButton;
	GtkHScale* m_pZoomScale;
	GtkEntry* m_pSearchBox;
	GtkImage* m_pStatusbarGPSIcon;
	GtkWidget *m_pSidebox;

	GtkButton* m_pZoomInButton;
	GtkMenuItem* m_pZoomInMenuItem;
	GtkButton* m_pZoomOutButton;
	GtkMenuItem* m_pZoomOutMenuItem;

	// Sidebar

	// "Draw" Sidebar (currently POI)
	GtkTreeView* m_pLayersListTreeView;
	GtkTreeView* m_pLocationSetsTreeView;
	GtkListStore* m_pLocationSetsListStore;
	GtkNotebook* m_pSidebarNotebook;

	// "GPS" sidebar
	GtkLabel* m_pSpeedLabel;	// these belong in m_GPS
	GtkProgressBar* m_pGPSSignalStrengthProgressBar;

	struct {
		GtkCheckButton* m_pShowPositionCheckButton;
		GtkCheckButton* m_pKeepPositionCenteredCheckButton;
		GtkCheckButton* m_pShowTrailCheckButton;
		GtkCheckButton* m_pStickToRoadsCheckButton;
	} m_GPS;

	// Statusbar
	GtkVBox*  m_pStatusbar;
 	GtkLabel* m_pPositionLabel;
	GtkLabel* m_pZoomScaleLabel;
	GtkProgressBar* m_pProgressBar;

	// Boxes
	GtkHBox* m_pContentBox;

	// Drawing area
	GtkDrawingArea* m_pDrawingArea;
	tooltip_t* m_pTooltip;
	map_t* m_pMap;

	EToolType m_eSelectedTool;

	gboolean m_bScrolling;
	EDirection m_eScrollDirection;
	gboolean m_bScrollMovement;

	gboolean m_bMouseDragging;
	gboolean m_bMouseDragMovement;

	screenpoint_t m_ptClickLocation;	

	gint m_nCurrentGPSPath;
	gint m_nGPSLocationGlyph;
	gint m_nDrawPrettyTimeoutID;
	gint m_nScrollTimeoutID;

	// Sliding
	gboolean m_bSliding;
	mappoint_t m_ptSlideStartLocation;
	mappoint_t m_ptSlideEndLocation;
	animator_t* m_pAnimator;

	// History (forward / back)
	history_t* m_pHistory;
	GtkButton* m_pForwardButton;
	GtkButton* m_pBackButton;
	GtkMenuItem* m_pForwardMenuItem;
	GtkMenuItem* m_pBackMenuItem;
} g_MainWindow = {0};


// Data
toolsettings_t g_Tools[] = {
	{"Pointer Tool", {GDK_LEFT_PTR, NULL}},
	{"Zoom Tool", {GDK_CIRCLE, NULL}},
};
void cursor_init()
{
	int i;
	for(i=0 ; i<G_N_ELEMENTS(g_Tools) ; i++) {
		g_Tools[i].m_Cursor.m_pGdkCursor = gdk_cursor_new(g_Tools[i].m_Cursor.m_CursorType);
	}
}

static void util_set_image_to_stock(GtkImage* pImage, gchar* pszStockIconID, GtkIconSize nSize)
{
	GdkPixbuf* pPixbuf = gtk_widget_render_icon(GTK_WIDGET(g_MainWindow.m_pStatusbarGPSIcon),pszStockIconID, nSize, "name");
	gtk_image_set_from_pixbuf(pImage, pPixbuf);
	g_object_unref(pPixbuf);
}

static void mainwindow_set_statusbar_position(gchar* pMessage)
{
	gtk_label_set_text(GTK_LABEL(g_MainWindow.m_pPositionLabel), pMessage);
}

static void mainwindow_set_statusbar_zoomscale(gchar* pMessage)
{
	gtk_label_set_text(GTK_LABEL(g_MainWindow.m_pZoomScaleLabel), pMessage);
}

void* mainwindow_set_busy(void)
{
	GdkCursor* pCursor = gdk_cursor_new(GDK_WATCH);
	// set it for the whole window
	gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pWindow)->window, pCursor);

	// HACK: children with their own cursors set need to be set manually and restored later :/
	gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window, pCursor);

	gdk_flush();
	return pCursor;
}

void mainwindow_set_not_busy(void** ppCursor)
{
	gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pWindow)->window, NULL);
	gdk_cursor_unref(*ppCursor);
	gdk_flush();

	// HACK: manually restore the cursor for selected tool :/
	mainwindow_setup_selected_tool();
}

/*
** Status bar
*/

void mainwindow_init(GladeXML* pGladeXML)
{
	GtkCellRenderer* pCellRenderer;
  	GtkTreeViewColumn* pColumn;

	// Widgets
	g_MainWindow.m_pWindow			= GTK_WINDOW(glade_xml_get_widget(pGladeXML, "mainwindow"));			g_return_if_fail(g_MainWindow.m_pWindow != NULL);
	g_MainWindow.m_pMapPopupMenu		= GTK_MENU(glade_xml_get_widget(pGladeXML, "mappopupmenu"));			g_return_if_fail(g_MainWindow.m_pMapPopupMenu != NULL);
//	g_MainWindow.m_pPointerToolButton 	= GTK_TOOL_BUTTON(glade_xml_get_widget(pGladeXML, "pointertoolbutton"));	g_return_if_fail(g_MainWindow.m_pPointerToolButton != NULL);
//	g_MainWindow.m_pZoomToolButton 		= GTK_TOOL_BUTTON(glade_xml_get_widget(pGladeXML, "zoomtoolbutton")); 		g_return_if_fail(g_MainWindow.m_pZoomToolButton != NULL);

	g_MainWindow.m_pZoomInButton		= GTK_BUTTON(glade_xml_get_widget(pGladeXML, "zoominbutton")); 			g_return_if_fail(g_MainWindow.m_pZoomInButton != NULL);
	g_MainWindow.m_pZoomInMenuItem		= GTK_MENU_ITEM(glade_xml_get_widget(pGladeXML, "zoominmenuitem")); 		g_return_if_fail(g_MainWindow.m_pZoomInMenuItem != NULL);
	g_MainWindow.m_pZoomOutButton		= GTK_BUTTON(glade_xml_get_widget(pGladeXML, "zoomoutbutton")); 		g_return_if_fail(g_MainWindow.m_pZoomOutButton != NULL);
	g_MainWindow.m_pZoomOutMenuItem		= GTK_MENU_ITEM(glade_xml_get_widget(pGladeXML, "zoomoutmenuitem")); 		g_return_if_fail(g_MainWindow.m_pZoomOutMenuItem != NULL);

	g_MainWindow.m_pContentBox 		= GTK_HBOX(glade_xml_get_widget(pGladeXML, "mainwindowcontentsbox"));		g_return_if_fail(g_MainWindow.m_pContentBox != NULL);
	g_MainWindow.m_pLocationSetsTreeView	= GTK_TREE_VIEW(glade_xml_get_widget(pGladeXML, "locationsetstreeview"));	g_return_if_fail(g_MainWindow.m_pLocationSetsTreeView != NULL);
	g_MainWindow.m_pLayersListTreeView	= GTK_TREE_VIEW(glade_xml_get_widget(pGladeXML, "layerstreeview"));		g_return_if_fail(g_MainWindow.m_pLayersListTreeView != NULL);
	g_MainWindow.m_pZoomScale		= GTK_HSCALE(glade_xml_get_widget(pGladeXML, "zoomscale"));			g_return_if_fail(g_MainWindow.m_pZoomScale != NULL);
	g_MainWindow.m_pPositionLabel 		= GTK_LABEL(glade_xml_get_widget(pGladeXML, "positionlabel"));			g_return_if_fail(g_MainWindow.m_pPositionLabel != NULL);
	g_MainWindow.m_pStatusbarGPSIcon 	= GTK_IMAGE(glade_xml_get_widget(pGladeXML, "statusbargpsicon"));		g_return_if_fail(g_MainWindow.m_pStatusbarGPSIcon != NULL);
	g_MainWindow.m_pZoomScaleLabel 		= GTK_LABEL(glade_xml_get_widget(pGladeXML, "zoomscalelabel"));			g_return_if_fail(g_MainWindow.m_pZoomScaleLabel != NULL);
	g_MainWindow.m_pToolbar			= GTK_TOOLBAR(glade_xml_get_widget(pGladeXML, "maintoolbar"));			g_return_if_fail(g_MainWindow.m_pToolbar != NULL);
	g_MainWindow.m_pStatusbar		= GTK_VBOX(glade_xml_get_widget(pGladeXML, "statusbar"));			g_return_if_fail(g_MainWindow.m_pStatusbar != NULL);
	g_MainWindow.m_pSidebox			= GTK_WIDGET(glade_xml_get_widget(pGladeXML, "mainwindowsidebox"));		g_return_if_fail(g_MainWindow.m_pSidebox != NULL);
	g_MainWindow.m_pSidebarNotebook		= GTK_NOTEBOOK(glade_xml_get_widget(pGladeXML, "sidebarnotebook"));		g_return_if_fail(g_MainWindow.m_pSidebarNotebook != NULL);
	g_MainWindow.m_pTooltips		= gtk_tooltips_new();
	g_MainWindow.m_pSpeedLabel		= GTK_LABEL(glade_xml_get_widget(pGladeXML, "speedlabel"));			g_return_if_fail(g_MainWindow.m_pSpeedLabel != NULL);

	// GPS Widgets
	g_MainWindow.m_GPS.m_pShowPositionCheckButton		= GTK_CHECK_BUTTON(glade_xml_get_widget(pGladeXML, "gpsshowpositioncheckbutton"));		g_return_if_fail(g_MainWindow.m_GPS.m_pShowPositionCheckButton != NULL);
	g_MainWindow.m_GPS.m_pKeepPositionCenteredCheckButton	= GTK_CHECK_BUTTON(glade_xml_get_widget(pGladeXML, "gpskeeppositioncenteredcheckbutton"));	g_return_if_fail(g_MainWindow.m_GPS.m_pKeepPositionCenteredCheckButton != NULL);
	g_MainWindow.m_GPS.m_pShowTrailCheckButton 		= GTK_CHECK_BUTTON(glade_xml_get_widget(pGladeXML, "gpsshowtrailcheckbutton"));			g_return_if_fail(g_MainWindow.m_GPS.m_pKeepPositionCenteredCheckButton != NULL);
	g_MainWindow.m_GPS.m_pStickToRoadsCheckButton 		= GTK_CHECK_BUTTON(glade_xml_get_widget(pGladeXML, "gpssticktoroadscheckbutton"));		g_return_if_fail(g_MainWindow.m_GPS.m_pStickToRoadsCheckButton != NULL);
	g_MainWindow.m_pGPSSignalStrengthProgressBar 		= GTK_PROGRESS_BAR(glade_xml_get_widget(pGladeXML, "gpssignalprogressbar"));			g_return_if_fail(g_MainWindow.m_pGPSSignalStrengthProgressBar != NULL);

	// History Widgets
	g_MainWindow.m_pForwardButton 		= GTK_BUTTON(glade_xml_get_widget(pGladeXML, "forwardbutton"));			g_return_if_fail(g_MainWindow.m_pForwardButton != NULL);
	g_MainWindow.m_pBackButton 		= GTK_BUTTON(glade_xml_get_widget(pGladeXML, "backbutton"));			g_return_if_fail(g_MainWindow.m_pBackButton != NULL);
	g_MainWindow.m_pForwardMenuItem 	= GTK_MENU_ITEM(glade_xml_get_widget(pGladeXML, "forwardmenuitem"));		g_return_if_fail(g_MainWindow.m_pForwardMenuItem != NULL);
	g_MainWindow.m_pBackMenuItem 		= GTK_MENU_ITEM(glade_xml_get_widget(pGladeXML, "backmenuitem"));		g_return_if_fail(g_MainWindow.m_pBackMenuItem != NULL);

	g_MainWindow.m_pHistory = history_new();
	g_assert(g_MainWindow.m_pHistory);

	g_signal_connect(G_OBJECT(g_MainWindow.m_pWindow), "delete_event", G_CALLBACK(gtk_main_quit), NULL);

	// create drawing area
	g_MainWindow.m_pDrawingArea = GTK_DRAWING_AREA(gtk_drawing_area_new());
	g_MainWindow.m_pTooltip = tooltip_new();

	// create map and load style
	map_new(&g_MainWindow.m_pMap, GTK_WIDGET(g_MainWindow.m_pDrawingArea));
	map_style_load(g_MainWindow.m_pMap, MAP_STYLE_FILENAME);

	// add signal handlers to drawing area
	gtk_widget_add_events(GTK_WIDGET(g_MainWindow.m_pDrawingArea), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "expose_event", G_CALLBACK(mainwindow_on_expose_event), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "configure_event", G_CALLBACK(mainwindow_on_configure_event), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "button_press_event", G_CALLBACK(mainwindow_on_mouse_button_click), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "button_release_event", G_CALLBACK(mainwindow_on_mouse_button_click), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "motion_notify_event", G_CALLBACK(mainwindow_on_mouse_motion), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "scroll_event", G_CALLBACK(mainwindow_on_mouse_scroll), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "enter_notify_event", G_CALLBACK(mainwindow_on_enter_notify), NULL);
	g_signal_connect(G_OBJECT(g_MainWindow.m_pDrawingArea), "leave_notify_event", G_CALLBACK(mainwindow_on_leave_notify), NULL);

	// Pack canvas into application window
	gtk_box_pack_end(GTK_BOX(g_MainWindow.m_pContentBox), GTK_WIDGET(g_MainWindow.m_pDrawingArea),
					TRUE, // expand
					TRUE, // fill
                                        0);
	gtk_widget_show(GTK_WIDGET(g_MainWindow.m_pDrawingArea));

	cursor_init();

        g_MainWindow.m_nGPSLocationGlyph = glyph_load(PACKAGE_DATA_DIR"/car.svg");

	// create location sets tree view
	g_MainWindow.m_pLocationSetsListStore = gtk_list_store_new(3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_view_set_model(g_MainWindow.m_pLocationSetsTreeView, GTK_TREE_MODEL(g_MainWindow.m_pLocationSetsListStore));

	// NEW COLUMN: Visible checkbox
	pCellRenderer = gtk_cell_renderer_toggle_new();
  	g_object_set_data(G_OBJECT(pCellRenderer), "column", (gint *)LAYERLIST_COLUMN_ENABLED);
	g_signal_connect(pCellRenderer, "toggled", G_CALLBACK(mainwindow_on_locationset_visible_checkbox_clicked), NULL);
	pColumn = gtk_tree_view_column_new_with_attributes("Visible", pCellRenderer, "active", LOCATIONSETLIST_COLUMN_ENABLED, NULL);
	gtk_tree_view_append_column(g_MainWindow.m_pLocationSetsTreeView, pColumn);

	// NEW COLUMN: Name column
	pCellRenderer = gtk_cell_renderer_text_new();
	pColumn = gtk_tree_view_column_new_with_attributes("Layers", pCellRenderer, "text", LOCATIONSETLIST_COLUMN_NAME, NULL);
	gtk_tree_view_append_column(g_MainWindow.m_pLocationSetsTreeView, pColumn);

	// Fill locationset list iwth live data
	const GPtrArray* pLocationSetArray = locationset_get_array();
	gint i;
	GtkTreeIter iter;

	for(i=0 ; i<pLocationSetArray->len ; i++) {
		locationset_t* pLocationSet = g_ptr_array_index(pLocationSetArray, i);

		gchar* pszName = g_strdup_printf("%s (%d)", pLocationSet->m_pszName, pLocationSet->m_nLocationCount);

		gtk_list_store_append(g_MainWindow.m_pLocationSetsListStore, &iter);
		gtk_list_store_set(g_MainWindow.m_pLocationSetsListStore, &iter, 
				   LOCATIONSETLIST_COLUMN_NAME, pszName, 
				   LOCATIONSETLIST_COLUMN_ENABLED, TRUE,
				   LOCATIONSETLIST_COLUMN_ID, pLocationSet->m_nID, 
				   -1);
		g_free(pszName);
	}

#ifdef ROADSTER_DEAD_CODE
/*
	// create layers tree view
	GtkListStore* pLayersListStore = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
	gtk_tree_view_set_model(g_MainWindow.m_pLayersListTreeView, GTK_TREE_MODEL(pLayersListStore));

	// add a checkbox column for layer enabled/disabled
	pCellRenderer = gtk_cell_renderer_toggle_new();
  	g_object_set_data (G_OBJECT (pCellRenderer ), "column", (gint *)LAYERLIST_COLUMN_ENABLED);
//	g_signal_connect (pCellRenderer, "toggled", G_CALLBACK (on_layervisible_checkbox_clicked), pLayersListStore);
	pColumn = gtk_tree_view_column_new_with_attributes("Visible", pCellRenderer, "active", LAYERLIST_COLUMN_ENABLED, NULL);
	gtk_tree_view_append_column(g_MainWindow.m_pLayersListTreeView, pColumn);

	// add Layer Name column (with a text renderer)
	pCellRenderer = gtk_cell_renderer_text_new();
	pColumn = gtk_tree_view_column_new_with_attributes("Layers", pCellRenderer, "text", LAYERLIST_COLUMN_NAME, NULL);
	gtk_tree_view_append_column(g_MainWindow.m_pLayersListTreeView, pColumn);
*/
#endif

	mainwindow_statusbar_update_zoomscale();
	mainwindow_statusbar_update_position();	

//	locationset_load_locationsets();
//	mainwindow_load_locationset_list();
	
	/* add some data to the layers list */
//         GtkTreeIter iter;
//
//         int i;
//         for(i=LAYER_FIRST ; i<=LAYER_LAST ; i++) {
//                 gboolean bEnabled = TRUE;
//
//                 gtk_list_store_append(GTK_LIST_STORE(pLayersListStore), &iter);
//                 gtk_list_store_set(GTK_LIST_STORE(pLayersListStore), &iter,
//                         LAYERLIST_COLUMN_ENABLED, bEnabled,
//                         LAYERLIST_COLUMN_NAME, g_aLayers[i].m_pszName,
//                         -1);
//         }

	// GPS check timeout
	g_timeout_add(TIMER_GPS_REDRAW_INTERVAL_MS, (GSourceFunc)mainwindow_callback_on_gps_redraw_timeout, (gpointer)NULL);

	// Slide timeout
//	g_MainWindow.m_pAnimator = animator_new(ANIMATIONTYPE_SLIDE, 10.0);
	g_timeout_add(SLIDE_TIMEOUT_MS, (GSourceFunc)mainwindow_callback_on_slide_timeout, (gpointer)NULL);

	// give it a call to init everything
	mainwindow_callback_on_gps_redraw_timeout(NULL);
}


void mainwindow_show(void)
{
	gtk_widget_show(GTK_WIDGET(g_MainWindow.m_pWindow));
	gtk_window_present(g_MainWindow.m_pWindow);
}

void mainwindow_hide(void)
{
	gtk_widget_hide(GTK_WIDGET(g_MainWindow.m_pWindow));
}

void mainwindow_set_sensitive(gboolean bSensitive)
{
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pWindow), bSensitive);
}

//
// the "draw pretty" timeout lets us draw ugly/fast graphics while moving, then redraw pretty after we stop
//
gboolean mainwindow_on_draw_pretty_timeout(gpointer _unused)
{
	mainwindow_draw_map(DRAWFLAG_ALL);

	// We've just drawn a complete frame, so we can consider this a new drag with no movement yet
	g_MainWindow.m_bMouseDragMovement = FALSE;	// NOTE: may have already been FALSE, for example if we're not dragging... :)

	// Returning FALSE from the timeout callback kills the timer.
	g_MainWindow.m_nDrawPrettyTimeoutID = 0;
	return FALSE;
}

void mainwindow_cancel_draw_pretty_timeout()
{
	if(g_MainWindow.m_nDrawPrettyTimeoutID != 0) {
		g_source_remove(g_MainWindow.m_nDrawPrettyTimeoutID);
	}
}

void mainwindow_set_draw_pretty_timeout(gint nTimeoutInMilliseconds)
{
	// cancel existing one, if one exists
	mainwindow_cancel_draw_pretty_timeout();

	g_MainWindow.m_nDrawPrettyTimeoutID = g_timeout_add(nTimeoutInMilliseconds, mainwindow_on_draw_pretty_timeout, NULL);
	g_assert(g_MainWindow.m_nDrawPrettyTimeoutID != 0);
}

//
// the "scroll" timeout
//
void mainwindow_scroll_direction(EDirection eScrollDirection, gint nPixels)
{
	if(eScrollDirection != DIRECTION_NONE) {
		gint nWidth = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width;
		gint nHeight = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height;

		gint nDeltaX = nPixels * g_aDirectionMultipliers[eScrollDirection].m_nX;
		gint nDeltaY = nPixels * g_aDirectionMultipliers[eScrollDirection].m_nY;

		mainwindow_map_center_on_windowpoint((nWidth / 2) + nDeltaX, (nHeight / 2) + nDeltaY);
		mainwindow_draw_map(DRAWFLAG_GEOMETRY);
		mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_SCROLL_TIMEOUT_MS);
	}
}

gboolean mainwindow_on_scroll_timeout(gpointer _unused)
{
	mainwindow_scroll_direction(g_MainWindow.m_eScrollDirection, SCROLL_DISTANCE_IN_PIXELS);
	g_MainWindow.m_bScrollMovement = TRUE;

	return TRUE;	// more events, please
}
void mainwindow_cancel_scroll_timeout()
{
	if(g_MainWindow.m_nScrollTimeoutID != 0) {
		g_source_remove(g_MainWindow.m_nScrollTimeoutID);
	}
}
void mainwindow_set_scroll_timeout()
{
	// cancel existing one, if one exists
	mainwindow_cancel_scroll_timeout();

	g_MainWindow.m_nScrollTimeoutID = g_timeout_add(SCROLL_TIMEOUT_MS, mainwindow_on_scroll_timeout, NULL);
	g_assert(g_MainWindow.m_nScrollTimeoutID != 0);
}


/*
** Toolbar
*/
void mainwindow_set_toolbar_visible(gboolean bVisible)
{
	if(bVisible) {
		gtk_widget_show(GTK_WIDGET(g_MainWindow.m_pToolbar));
	}
	else {
		gtk_widget_hide(GTK_WIDGET(g_MainWindow.m_pToolbar));
	}
}

gboolean mainwindow_get_toolbar_visible(void)
{
	return GTK_WIDGET_VISIBLE(g_MainWindow.m_pToolbar);
}

/*
** Statusbar
*/
void mainwindow_set_statusbar_visible(gboolean bVisible)
{
	if(bVisible) {
		gtk_widget_show(GTK_WIDGET(g_MainWindow.m_pStatusbar));
	}
	else {
		gtk_widget_hide(GTK_WIDGET(g_MainWindow.m_pStatusbar));
	}
}

gboolean mainwindow_get_statusbar_visible(void)
{
	return GTK_WIDGET_VISIBLE(g_MainWindow.m_pStatusbar);
}

void mainwindow_statusbar_update_zoomscale(void)
{
	char buf[200];
	guint32 uZoomLevelScale = map_get_zoomlevel_scale(g_MainWindow.m_pMap);

	snprintf(buf, 199, "1:%d", uZoomLevelScale);
	mainwindow_set_statusbar_zoomscale(buf);
//	GTK_PROCESS_MAINLOOP;
}

void mainwindow_statusbar_update_position(void)
{
	char buf[200];
	mappoint_t pt;
	map_get_centerpoint(g_MainWindow.m_pMap, &pt);
	g_snprintf(buf, 200, "Lat: %.5f, Lon: %.5f", pt.m_fLatitude, pt.m_fLongitude);
	mainwindow_set_statusbar_position(buf);
//	GTK_PROCESS_MAINLOOP;    ugh this makes text flash when stopping scrolling
}

/*
** Sidebox (Layers, etc.)
*/
void mainwindow_set_sidebox_visible(gboolean bVisible)
{
	if(bVisible) {
		gtk_widget_show(GTK_WIDGET(g_MainWindow.m_pSidebox));
	}
	else {
		gtk_widget_hide(GTK_WIDGET(g_MainWindow.m_pSidebox));
	}
}

gboolean mainwindow_get_sidebox_visible(void)
{
	return GTK_WIDGET_VISIBLE(g_MainWindow.m_pSidebox);
}

GtkWidget* mainwindow_get_window(void)
{
	return GTK_WIDGET(g_MainWindow.m_pWindow);
}

void mainwindow_toggle_fullscreen(void)
{
    GdkWindow* toplevelgdk = gdk_window_get_toplevel(GTK_WIDGET(g_MainWindow.m_pWindow)->window);
	if(toplevelgdk) {
		GdkWindowState windowstate = gdk_window_get_state(toplevelgdk);
		if(windowstate & GDK_WINDOW_STATE_FULLSCREEN) {
			gdk_window_unfullscreen(toplevelgdk);
		}
		else {
			gdk_window_fullscreen(toplevelgdk);
		}
	}
}

// User clicked Quit window
void mainwindow_on_quitmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	gui_exit();
}

// User closed main window
gboolean mainwindow_on_application_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	gui_exit();
	return FALSE; // satisfy strick compiler
}

//
// Zoom
//
void mainwindow_update_zoom_buttons()
{
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pZoomInButton), map_can_zoom_in(g_MainWindow.m_pMap));
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pZoomInMenuItem), map_can_zoom_in(g_MainWindow.m_pMap));
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pZoomOutButton), map_can_zoom_out(g_MainWindow.m_pMap));
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pZoomOutMenuItem), map_can_zoom_out(g_MainWindow.m_pMap));
}

void mainwindow_set_zoomlevel(gint nZoomLevel)
{
	map_set_zoomlevel(g_MainWindow.m_pMap, nZoomLevel);

	// set zoomlevel scale but prevent it from calling handler (mainwindow_on_zoomscale_value_changed)
        g_signal_handlers_block_by_func(g_MainWindow.m_pZoomScale, mainwindow_on_zoomscale_value_changed, NULL);
	gtk_range_set_value(GTK_RANGE(g_MainWindow.m_pZoomScale), nZoomLevel);
	g_signal_handlers_unblock_by_func(g_MainWindow.m_pZoomScale, mainwindow_on_zoomscale_value_changed, NULL);

	mainwindow_statusbar_update_zoomscale();
	mainwindow_update_zoom_buttons();
}

// the range slider changed value
void mainwindow_on_zoomscale_value_changed(GtkRange *range, gpointer user_data)
{
	gdouble fValue = gtk_range_get_value(range);
	gint16 nValue = (gint16)fValue;

	// update GUI
	mainwindow_set_zoomlevel(nValue);

	// also redraw immediately and add history item
	mainwindow_draw_map(DRAWFLAG_GEOMETRY);
	mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_ZOOM_TIMEOUT_MS);

	mainwindow_add_history();
}

//
//
//
static void gui_set_tool(EToolType eTool)
{
	g_MainWindow.m_eSelectedTool = eTool;
	gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window, g_Tools[eTool].m_Cursor.m_pGdkCursor);
}

void callback_url_clicked(GtkAboutDialog *about, const gchar *link, gpointer data)
{
	g_print("opening URL: %s\n", link);
	util_open_uri(link);
}

void callback_email_clicked(GtkAboutDialog *about, const gchar *link, gpointer data)
{
	gchar* pszURI = g_strdup_printf("mailto:%s", link);
	util_open_uri(pszURI);
	g_free(pszURI);

}

void mainwindow_on_aboutmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
#if(GLIB_CHECK_VERSION(2,6,0))
         const gchar *ppAuthors[] = {
                 "Ian McIntosh <ian_mcintosh@linuxadvocate.org>",
                 "Nathan Fredrickson <nathan@silverorange.com>",
                 NULL
         };

         const gchar *ppArtists[] = {
				"Stephen DesRoches <stephen@silverorange.com>",
                 NULL
         };

		 GdkPixbuf* pIconPixbuf;
		 gchar* pszPath;

		 pszPath = g_strdup_printf(PACKAGE_SOURCE_DIR"/data/%s", "roadster-logo.png");
		 pIconPixbuf = gdk_pixbuf_new_from_file(pszPath, NULL);
		 g_free(pszPath);

		 if(pIconPixbuf == NULL) {
			 pszPath = g_strdup_printf(PACKAGE_DATA_DIR"/data/%s", "roadster-logo.png");
			 pIconPixbuf = gdk_pixbuf_new_from_file(pszPath, NULL);
			 g_free(pszPath);
		 }

         gtk_about_dialog_set_url_hook(callback_url_clicked, NULL, NULL);
         gtk_about_dialog_set_email_hook(callback_email_clicked, NULL, NULL);
		 gtk_show_about_dialog(g_MainWindow.m_pWindow,
				   "authors", ppAuthors,
			       "artists", ppArtists,
			       "comments", PROGRAM_DESCRIPTION,
			       "copyright", PROGRAM_COPYRIGHT,
			       "name", PROGRAM_NAME,
				    "website", WEBSITE_URL,
				   "website-label", "Visit the Roadster Website",
			       "version", VERSION,
				   "logo", pIconPixbuf,
			       NULL);

		 if(pIconPixbuf) g_object_unref(pIconPixbuf);
#endif
}

// Toggle toolbar visibility
void mainwindow_on_toolbarmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	mainwindow_set_toolbar_visible( !mainwindow_get_toolbar_visible() );
}

// Toggle statusbar visibility
void mainwindow_on_statusbarmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	mainwindow_set_statusbar_visible( !mainwindow_get_statusbar_visible() );
}

void mainwindow_on_sidebarmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	mainwindow_set_sidebox_visible(!mainwindow_get_sidebox_visible());
}

// Zoom buttons / menu items (shared callbacks)
void mainwindow_on_zoomin_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	// tell the map
	map_set_zoomlevel(g_MainWindow.m_pMap, map_get_zoomlevel(g_MainWindow.m_pMap) + 1);
	mainwindow_set_zoomlevel(map_get_zoomlevel(g_MainWindow.m_pMap));
	mainwindow_draw_map(DRAWFLAG_GEOMETRY);
	mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_ZOOM_TIMEOUT_MS);
	mainwindow_add_history();
}

void mainwindow_on_zoomout_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	map_set_zoomlevel(g_MainWindow.m_pMap, map_get_zoomlevel(g_MainWindow.m_pMap) - 1);
	mainwindow_set_zoomlevel(map_get_zoomlevel(g_MainWindow.m_pMap));
	mainwindow_draw_map(DRAWFLAG_GEOMETRY);
	mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_ZOOM_TIMEOUT_MS);
	mainwindow_add_history();
}

void mainwindow_on_fullscreenmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	mainwindow_toggle_fullscreen();
}

void mainwindow_on_gotomenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	gotowindow_show();
}

void mainwindow_on_reloadstylesmenuitem_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	map_style_load(g_MainWindow.m_pMap, MAP_STYLE_FILENAME);
	mainwindow_draw_map(DRAWFLAG_ALL);
}

EDirection match_border(gint nX, gint nY, gint nWidth, gint nHeight, gint nBorderSize)
{
	EDirection eDirection;

	// Corner hit targets are L shaped and 1/3 of the two borders it touches
	gint nXCorner = nWidth/3;
	gint nYCorner = nHeight/3;

	// LEFT EDGE?
	if(nX <= nBorderSize) {
		if(nY <= nYCorner) {
			eDirection = DIRECTION_NW;
		}
		else if((nY+nYCorner) >= nHeight) {
			eDirection = DIRECTION_SW;
		}
		else {
			eDirection = DIRECTION_W;
		}
	}
	// RIGHT EDGE?
	else if((nX+nBorderSize) >= nWidth) {
		if(nY <= nYCorner) {
			eDirection = DIRECTION_NE;
		}
		else if((nY+nYCorner) >= nHeight) {
			eDirection = DIRECTION_SE;
		}
		else {
			eDirection = DIRECTION_E;
		}
	}
	// TOP?
	else if(nY <= nBorderSize) {
		if(nX <= nXCorner) {
			eDirection = DIRECTION_NW;
		}
		else if((nX+nXCorner) >= nWidth) {
			eDirection = DIRECTION_NE;
		}
		else {
			eDirection = DIRECTION_N;
		}
	}
	// BOTTOM?
	else if((nY+nBorderSize) >= nHeight) {
		if(nX <= nXCorner) {
			eDirection = DIRECTION_SW;
		}
		else if((nX+nXCorner) >= nWidth) {
			eDirection = DIRECTION_SE;
		}
		else {
			eDirection = DIRECTION_S;
		}
	}
	// center.
	else {
		eDirection = DIRECTION_NONE;
	}
	return eDirection;
}

static gboolean mainwindow_on_mouse_button_click(GtkWidget* w, GdkEventButton *event)
{
	gint nX, nY;
	gdk_window_get_pointer(w->window, &nX, &nY, NULL);

	gint nWidth = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width;
	gint nHeight = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height;
	EDirection eScrollDirection = DIRECTION_NONE;

	// get mouse position on screen
	screenpoint_t screenpoint = {nX,nY};
	mappoint_t mappoint;
	map_windowpoint_to_mappoint(g_MainWindow.m_pMap, &screenpoint, &mappoint);
	
	maphit_t* pHitStruct = NULL;
	map_hit_test(g_MainWindow.m_pMap, &mappoint, &pHitStruct);
	// hitstruct free'd far below

	if(event->button == MOUSE_BUTTON_LEFT) {
		// Left mouse button down?
		if(event->type == GDK_BUTTON_PRESS) {
			tooltip_hide(g_MainWindow.m_pTooltip);

			// Is it at a border?
			eScrollDirection = match_border(nX, nY, nWidth, nHeight, BORDER_SCROLL_CLICK_TARGET_SIZE);
			if(eScrollDirection != DIRECTION_NONE) {
				// begin a scroll
				GdkCursor* pCursor = gdk_cursor_new(g_aDirectionCursors[eScrollDirection].m_nCursor);
				gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window, pCursor);
				gdk_cursor_unref(pCursor);

				g_MainWindow.m_bScrolling = TRUE;
				g_MainWindow.m_eScrollDirection = eScrollDirection;
				g_MainWindow.m_bScrollMovement = FALSE;	// no movement yet

				mainwindow_set_scroll_timeout();
			}
			else {
				g_MainWindow.m_bMouseDragging = TRUE;
				g_MainWindow.m_bMouseDragMovement = FALSE;
				g_MainWindow.m_ptClickLocation.m_nX = nX;
				g_MainWindow.m_ptClickLocation.m_nY = nY;
			}
		}
		// Left mouse button up?
		else if(event->type == GDK_BUTTON_RELEASE) {
			// end mouse dragging, if active
			if(g_MainWindow.m_bMouseDragging == TRUE) {
				// restore cursor
				GdkCursor* pCursor = gdk_cursor_new(GDK_LEFT_PTR);
				gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window, pCursor);
				gdk_cursor_unref(pCursor);

				g_MainWindow.m_bMouseDragging = FALSE;
				if(g_MainWindow.m_bMouseDragMovement) {
					mainwindow_cancel_draw_pretty_timeout();
					mainwindow_draw_map(DRAWFLAG_ALL);

					mainwindow_add_history();
				}
				else if(pHitStruct != NULL) {
					if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATION) {
						if(map_location_selection_add(g_MainWindow.m_pMap, pHitStruct->m_LocationHit.m_nLocationID)) {
							mainwindow_draw_map(DRAWFLAG_ALL);
						}
					}
					else if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATIONSELECTION_CLOSE) {
						if(map_location_selection_remove(g_MainWindow.m_pMap, pHitStruct->m_LocationHit.m_nLocationID)) {
							mainwindow_draw_map(DRAWFLAG_ALL);
						}
					}
					else if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATIONSELECTION_EDIT) {
						// edit POI
						//g_MainWindow.m_pMap, pHitStruct->m_LocationHit.m_nLocationID
					}
					else if(pHitStruct->m_eHitType == MAP_HITTYPE_URL) {
						util_open_uri(pHitStruct->m_URLHit.m_pszURL);
					}
				}
			}

			// end scrolling, if active
			if(g_MainWindow.m_bScrolling == TRUE) {
				// NOTE: don't restore cursor (mouse could *still* be over screen edge)

				g_MainWindow.m_bScrolling = FALSE;
				mainwindow_cancel_draw_pretty_timeout();

				// has there been any movement?
				if(g_MainWindow.m_bScrollMovement) {
					g_MainWindow.m_bScrollMovement = FALSE;
					mainwindow_draw_map(DRAWFLAG_ALL);
				}
				else {
					// user clicked the edge of the screen, but so far we haven't moved at all (they released the button too fast)
					// so consider this a 'click' and just jump a bit in that direction
					gint nHeight = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height;
					gint nWidth = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width;

					gint nDistanceInPixels;
					if(g_MainWindow.m_eScrollDirection == DIRECTION_N || g_MainWindow.m_eScrollDirection == DIRECTION_S) {
						// scroll half the height of the screen
						nDistanceInPixels = nHeight/2;
					}
					else if(g_MainWindow.m_eScrollDirection == DIRECTION_E || g_MainWindow.m_eScrollDirection == DIRECTION_W) {
						nDistanceInPixels = nWidth/2;
					}
					else {
						// half the distance from corner to opposite corner
						nDistanceInPixels = sqrt(nHeight*nHeight + nWidth*nWidth)/2;
					}

					mainwindow_scroll_direction(g_MainWindow.m_eScrollDirection, nDistanceInPixels);
				}
				g_MainWindow.m_eScrollDirection = DIRECTION_NONE;
				mainwindow_add_history();
			}
		}
		else if(event->type == GDK_2BUTTON_PRESS) {
			// can only double click in the middle (not on a scroll border)
			eScrollDirection = match_border(nX, nY, nWidth, nHeight, BORDER_SCROLL_CLICK_TARGET_SIZE);
			if(eScrollDirection == DIRECTION_NONE) {
				animator_destroy(g_MainWindow.m_pAnimator);

				g_MainWindow.m_bSliding = TRUE;
				g_MainWindow.m_pAnimator = animator_new(ANIMATIONTYPE_FAST_THEN_SLIDE, SLIDE_TIME_IN_SECONDS);

				// set startpoint
				map_get_centerpoint(g_MainWindow.m_pMap, &g_MainWindow.m_ptSlideStartLocation);

				// set endpoint
				screenpoint_t ptScreenPoint = {nX, nY};
				map_windowpoint_to_mappoint(g_MainWindow.m_pMap, &ptScreenPoint, &(g_MainWindow.m_ptSlideEndLocation));
			}
		}
	}
	else if (event->button == MOUSE_BUTTON_RIGHT) {
		// single right-click?
		if(event->type == GDK_BUTTON_PRESS) {
			// Save click location for use by callback
	//		g_MainWindow.m_ptClickLocation.m_nX = nX;
	//		g_MainWindow.m_ptClickLocation.m_nY = nY;
			// Show popup!
	//		gtk_menu_popup(g_MainWindow.m_pMapPopupMenu, NULL, NULL, NULL, NULL, event->button, event->time);
	//		return TRUE;
		}
	}
	map_hitstruct_free(g_MainWindow.m_pMap, pHitStruct);
	return TRUE;
}

static gboolean mainwindow_on_mouse_motion(GtkWidget* w, GdkEventMotion *event)
{
    gint nX,nY;

	// XXX: why do we do this?
	if (event->is_hint) {
		gdk_window_get_pointer(w->window, &nX, &nY, NULL);
	}
	else {
		nX = event->x;
		nY = event->y;
	}

	gint nWidth = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width;
	gint nHeight = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height;

	gint nCursor = GDK_LEFT_PTR;

	if(g_MainWindow.m_bMouseDragging) {
		g_MainWindow.m_bMouseDragMovement = TRUE;

		// Set cursor here and not when first clicking because now we know it's a drag (on click it could be a double-click)
		nCursor = GDK_FLEUR;

		gint nDeltaX = g_MainWindow.m_ptClickLocation.m_nX - nX;
		gint nDeltaY = g_MainWindow.m_ptClickLocation.m_nY - nY;

		if(nDeltaX == 0 && nDeltaY == 0) return TRUE;

		mainwindow_map_center_on_windowpoint((nWidth / 2) + nDeltaX, (nHeight / 2) + nDeltaY);
		mainwindow_draw_map(DRAWFLAG_GEOMETRY);
		mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_DRAG_TIMEOUT_MS);

		g_MainWindow.m_ptClickLocation.m_nX = nX;
		g_MainWindow.m_ptClickLocation.m_nY = nY;
	}
	// set appropriate mouse cursor whether scrolling or not
	else if(g_MainWindow.m_bScrolling) {
		// just set the cursor the window

		EDirection eScrollDirection = match_border(nX, nY, nWidth, nHeight, BORDER_SCROLL_CLICK_TARGET_SIZE);

		// update cursor
		nCursor = g_aDirectionCursors[eScrollDirection].m_nCursor;

		// update direction if actively scrolling
		g_MainWindow.m_eScrollDirection = eScrollDirection;

//		if(g_MainWindow.m_eScrollDirection != eScrollDirection) {
			//GdkCursor* pCursor = gdk_cursor_new(g_aDirectionCursors[eScrollDirection].m_nCursor);
			//gdk_pointer_ungrab(GDK_CURRENT_TIME);
			//gdk_pointer_grab(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window, FALSE, GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_RELEASE_MASK, NULL, pCursor, GDK_CURRENT_TIME);
			//gdk_cursor_unref(pCursor);
//		}
	}
	else {
		// If not dragging or scrolling, user is just moving mouse around.
		// Update tooltip and mouse cursor based on what we're pointing at.

		EDirection eScrollDirection = match_border(nX, nY, nWidth, nHeight, BORDER_SCROLL_CLICK_TARGET_SIZE);

		if(eScrollDirection == DIRECTION_NONE) {
			// get mouse position on screen
			screenpoint_t screenpoint;
			screenpoint.m_nX = nX;
			screenpoint.m_nY = nY;
			mappoint_t mappoint;
			map_windowpoint_to_mappoint(g_MainWindow.m_pMap, &screenpoint, &mappoint);
	
			// try to "hit" something on the map. a road, a location, whatever!
			maphit_t* pHitStruct = NULL;
			if(map_hit_test(g_MainWindow.m_pMap, &mappoint, &pHitStruct)) {
				// A hit!  Move the tooltip here, format the text, and show it.
				tooltip_set_upper_left_corner(g_MainWindow.m_pTooltip, (gint)(event->x_root) + TOOLTIP_OFFSET_X, (gint)(event->y_root) + TOOLTIP_OFFSET_Y);

				gchar* pszMarkup = g_strdup_printf(TOOLTIP_FORMAT, pHitStruct->m_pszText);
				tooltip_set_markup(g_MainWindow.m_pTooltip, pszMarkup);
				g_free(pszMarkup);

				// choose color based on type of hit
				if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATION) {
					GdkColor clr = {0, LOCATION_TOOLTIP_BG_COLOR};
					tooltip_set_bg_color(g_MainWindow.m_pTooltip, &clr);

					// also set mouse cursor to hand
					nCursor = GDK_HAND2;
					tooltip_show(g_MainWindow.m_pTooltip);
				}
				else if(pHitStruct->m_eHitType == MAP_HITTYPE_ROAD) {
					GdkColor clr = {0, ROAD_TOOLTIP_BG_COLOR};
					tooltip_set_bg_color(g_MainWindow.m_pTooltip, &clr);
					tooltip_show(g_MainWindow.m_pTooltip);
				}
				else if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATIONSELECTION) {
					tooltip_hide(g_MainWindow.m_pTooltip);
				}
				else if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATIONSELECTION_CLOSE) {
					nCursor = GDK_HAND2;
					tooltip_hide(g_MainWindow.m_pTooltip);
				}
				else if(pHitStruct->m_eHitType == MAP_HITTYPE_LOCATIONSELECTION_EDIT) {
					nCursor = GDK_HAND2;
					tooltip_hide(g_MainWindow.m_pTooltip);
				}
				else if(pHitStruct->m_eHitType == MAP_HITTYPE_URL) {
					nCursor = GDK_HAND2;
					tooltip_hide(g_MainWindow.m_pTooltip);
				}

				map_hitstruct_free(g_MainWindow.m_pMap, pHitStruct);
			}
			else {
				// no hit. hide the tooltip
				tooltip_hide(g_MainWindow.m_pTooltip);
			}
		}
		else {
			nCursor = g_aDirectionCursors[eScrollDirection].m_nCursor;

			// using a funky (non-pointer) cursor so hide the tooltip
			tooltip_hide(g_MainWindow.m_pTooltip);
		}
	}
	// apply cursor that was chosen above
	GdkCursor* pCursor = gdk_cursor_new(nCursor);
	gdk_window_set_cursor(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window, pCursor);
	gdk_cursor_unref(pCursor);

	return FALSE;
}

static gboolean mainwindow_on_enter_notify(GtkWidget* w, GdkEventCrossing *event)
{
}

static gboolean mainwindow_on_leave_notify(GtkWidget* w, GdkEventCrossing *event)
{
	tooltip_hide(g_MainWindow.m_pTooltip);
}

static gboolean mainwindow_on_mouse_scroll(GtkWidget* w, GdkEventScroll *event)
{
	// respond to scroll wheel events by zooming in and out
	if(event->direction == GDK_SCROLL_UP) {
		map_set_zoomlevel(g_MainWindow.m_pMap, map_get_zoomlevel(g_MainWindow.m_pMap) + 1);
		mainwindow_set_zoomlevel(map_get_zoomlevel(g_MainWindow.m_pMap));
		mainwindow_draw_map(DRAWFLAG_GEOMETRY);
		mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_ZOOM_TIMEOUT_MS);
	}
	else if(event->direction == GDK_SCROLL_DOWN) {
		map_set_zoomlevel(g_MainWindow.m_pMap, map_get_zoomlevel(g_MainWindow.m_pMap) - 1);
		mainwindow_set_zoomlevel(map_get_zoomlevel(g_MainWindow.m_pMap));
		mainwindow_draw_map(DRAWFLAG_GEOMETRY);
		mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_ZOOM_TIMEOUT_MS);
	}
}

static void mainwindow_begin_import_geography_data(void)
{
g_print("starting..\n");

	GtkWidget* pDialog = gtk_file_chooser_dialog_new(
						"Select TIGER .ZIP files for Import",
                		g_MainWindow.m_pWindow,
		                GTK_FILE_CHOOSER_ACTION_OPEN,
						GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						"Import", GTK_RESPONSE_ACCEPT,
						NULL);

g_print("setting..\n");
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(pDialog), TRUE);
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pWindow), FALSE);

g_print("running..\n");
	gint nResponse = gtk_dialog_run(GTK_DIALOG(pDialog));
	gtk_widget_hide(pDialog);

	if(nResponse == GTK_RESPONSE_ACCEPT) {
		GSList* pSelectedFilesList = gtk_file_chooser_get_uris(GTK_FILE_CHOOSER(pDialog));

		importwindow_begin(pSelectedFilesList);

		// free each URI item with g_free
		GSList* pFile = pSelectedFilesList;
		while(pFile != NULL) {
			g_free(pFile->data); pFile->data = NULL;
			pFile = pFile->next;	// Move to next file
		}
		g_slist_free(pSelectedFilesList);
	}
	// re-enable main window and destroy dialog
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pWindow), TRUE);
	gtk_widget_destroy(pDialog);
}

void mainwindow_on_import_maps_activate(GtkWidget *widget, gpointer user_data)
{
	mainwindow_begin_import_geography_data();
}

static void mainwindow_setup_selected_tool(void)
{
//     if(gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(g_MainWindow.m_pPointerToolButton))) {
//         gui_set_tool(kToolPointer);
//     }
//     else if(gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(g_MainWindow.m_pZoomToolButton))) {
//         gui_set_tool(kToolZoom);
//     }
}

// One handler for all 'tools' buttons
void mainwindow_on_toolbutton_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
	// XXX: current there are no tools!
	mainwindow_setup_selected_tool();
}

void mainwindow_draw_map(gint nDrawFlags)
{
	map_draw(g_MainWindow.m_pMap, g_MainWindow.m_pMap->m_pPixmap, nDrawFlags);

	// push it to screen
	GdkPixmap* pMapPixmap = map_get_pixmap(g_MainWindow.m_pMap);
	gdk_draw_drawable(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window,
		      GTK_WIDGET(g_MainWindow.m_pDrawingArea)->style->fg_gc[GTK_WIDGET_STATE(g_MainWindow.m_pDrawingArea)],
		      pMapPixmap,
		      0,0,
		      0,0,
		      GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width, GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height);
	map_release_pixmap(g_MainWindow.m_pMap);
}

static gint mainwindow_on_configure_event(GtkWidget *pDrawingArea, GdkEventConfigure *event)
{
	// tell the map how big to draw
	dimensions_t dim;
	dim.m_uWidth = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width;
	dim.m_uHeight = GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height;
	map_set_dimensions(g_MainWindow.m_pMap, &dim);

	mainwindow_draw_map(DRAWFLAG_GEOMETRY);
	mainwindow_set_draw_pretty_timeout(DRAW_PRETTY_RESIZE_TIMEOUT_MS);
	return TRUE;
}

static gboolean mainwindow_on_expose_event(GtkWidget *pDrawingArea, GdkEventExpose *event, gpointer data)
{
	GdkPixmap* pMapPixmap = map_get_pixmap(g_MainWindow.m_pMap);

	// Copy relevant portion of off-screen bitmap to window
	gdk_draw_drawable(GTK_WIDGET(g_MainWindow.m_pDrawingArea)->window,
                      GTK_WIDGET(g_MainWindow.m_pDrawingArea)->style->fg_gc[GTK_WIDGET_STATE(g_MainWindow.m_pDrawingArea)],
                      pMapPixmap,
                      event->area.x, event->area.y,
                      event->area.x, event->area.y,
                      event->area.width, event->area.height);

	map_release_pixmap(g_MainWindow.m_pMap);
	return FALSE;
}

/*
** GPS Functions
*/
gboolean mainwindow_on_gps_show_position_toggled(GtkWidget* _unused, gpointer* __unused)
{

}

gboolean mainwindow_on_gps_keep_position_centered_toggled(GtkWidget* _unused, gpointer* __unused)
{

}

gboolean mainwindow_on_gps_show_trail_toggled(GtkWidget* _unused, gpointer* __unused)
{

}

gboolean mainwindow_on_gps_stick_to_roads_toggled(GtkWidget* _unused, gpointer* __unused)
{

}

static gboolean mainwindow_callback_on_gps_redraw_timeout(gpointer __unused)
{
	// NOTE: we're setting tooltips on the image's
	GtkWidget* pWidget = gtk_widget_get_parent(GTK_WIDGET(g_MainWindow.m_pStatusbarGPSIcon));

	const gpsdata_t* pData = gpsclient_getdata();
	if(pData->m_eStatus == GPS_STATUS_LIVE) {

		if(g_MainWindow.m_nCurrentGPSPath == 0) {
			// create a new track for GPS trail
			g_MainWindow.m_nCurrentGPSPath = track_new();
			map_add_track(g_MainWindow.m_pMap, g_MainWindow.m_nCurrentGPSPath);
		}

		track_add_point(g_MainWindow.m_nCurrentGPSPath, &pData->m_ptPosition);

		// Show position?
		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_MainWindow.m_GPS.m_pShowPositionCheckButton))) {
			// Keep it centered?
			if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_MainWindow.m_GPS.m_pKeepPositionCenteredCheckButton))) {
				map_set_centerpoint(g_MainWindow.m_pMap, &pData->m_ptPosition);
				mainwindow_statusbar_update_position();
			}

			// redraw because GPS icon/trail may be different
			mainwindow_draw_map(DRAWFLAG_ALL);
		}

		// update image and tooltip for GPS icon
		util_set_image_to_stock(g_MainWindow.m_pStatusbarGPSIcon, GTK_STOCK_OK, GTK_ICON_SIZE_MENU);
		gtk_tooltips_set_tip(GTK_TOOLTIPS(g_MainWindow.m_pTooltips), pWidget,
				 "A GPS device is present and active", "");

		// update speed
		gchar* pszSpeed = g_strdup_printf(SPEED_LABEL_FORMAT, pData->m_fSpeedInMilesPerHour);
		gtk_label_set_markup(g_MainWindow.m_pSpeedLabel, pszSpeed);
		g_free(pszSpeed);

		gtk_progress_bar_set_fraction(g_MainWindow.m_pGPSSignalStrengthProgressBar, pData->m_fSignalQuality * 20.0/100.0);
	}
	else {
		// unless we're "LIVE", set signal strength to 0
		gtk_progress_bar_set_fraction(g_MainWindow.m_pGPSSignalStrengthProgressBar, 0.0);

		if(pData->m_eStatus == GPS_STATUS_NO_GPS_COMPILED_IN) {
			util_set_image_to_stock(g_MainWindow.m_pStatusbarGPSIcon, GTK_STOCK_CANCEL, GTK_ICON_SIZE_MENU);
			
			gtk_tooltips_set_tip(GTK_TOOLTIPS(g_MainWindow.m_pTooltips), pWidget,
					 "GPS support was not enabled at compile time", "");
		}
		else if(pData->m_eStatus == GPS_STATUS_NO_SIGNAL) {
			// do NOT set speed to 0 if we drop the signal temporarily...
			util_set_image_to_stock(g_MainWindow.m_pStatusbarGPSIcon, GTK_STOCK_CANCEL, GTK_ICON_SIZE_MENU);

			gtk_tooltips_set_tip(GTK_TOOLTIPS(g_MainWindow.m_pTooltips), pWidget,
					 "A GPS device is present but unable to hear satellites", "");
		}
		else {
			// update speed, set to 0
			gchar* pszSpeed = g_strdup_printf(SPEED_LABEL_FORMAT, 0.0);
			gtk_label_set_markup(g_MainWindow.m_pSpeedLabel, pszSpeed);
			g_free(pszSpeed);

			if(pData->m_eStatus == GPS_STATUS_NO_DEVICE) {
				util_set_image_to_stock(g_MainWindow.m_pStatusbarGPSIcon, GTK_STOCK_STOP, GTK_ICON_SIZE_MENU);
				gtk_tooltips_set_tip(GTK_TOOLTIPS(g_MainWindow.m_pTooltips), pWidget,
						 "No GPS device is present", "");
			}
			else if(pData->m_eStatus == GPS_STATUS_NO_GPSD) {
				util_set_image_to_stock(g_MainWindow.m_pStatusbarGPSIcon, GTK_STOCK_STOP, GTK_ICON_SIZE_MENU);
				gtk_tooltips_set_tip(GTK_TOOLTIPS(g_MainWindow.m_pTooltips), pWidget,
						 "Install package 'gpsd' to use a GPS device with Roadster", "");
			}
			else {
				g_assert_not_reached();
			}
		}
	}
	return TRUE;
}

static gboolean mainwindow_callback_on_slide_timeout(gpointer pData)
{
	if(g_MainWindow.m_bSliding) {
		g_assert(g_MainWindow.m_pAnimator != NULL);

		// Figure out the progress along the path and interpolate 
		// between startpoint and endpoint

		gdouble fPercent = animator_get_progress(g_MainWindow.m_pAnimator);
		gboolean bDone = animator_is_done(g_MainWindow.m_pAnimator);
		if(bDone) {
			animator_destroy(g_MainWindow.m_pAnimator);
			g_MainWindow.m_pAnimator = NULL;
			g_MainWindow.m_bSliding = FALSE;

			fPercent = 1.0;

			mainwindow_add_history();
			// should we delete the timer?
		}

		gdouble fDeltaLat = g_MainWindow.m_ptSlideEndLocation.m_fLatitude - g_MainWindow.m_ptSlideStartLocation.m_fLatitude;
		gdouble fDeltaLon = g_MainWindow.m_ptSlideEndLocation.m_fLongitude - g_MainWindow.m_ptSlideStartLocation.m_fLongitude;

		mappoint_t ptNew;
		ptNew.m_fLatitude = g_MainWindow.m_ptSlideStartLocation.m_fLatitude + (fPercent * fDeltaLat);
		ptNew.m_fLongitude = g_MainWindow.m_ptSlideStartLocation.m_fLongitude + (fPercent * fDeltaLon);

		mainwindow_map_center_on_mappoint(&ptNew);
		if(bDone) {
			// when done, draw a full frame
			//mainwindow_draw_map(DRAWFLAG_GEOMETRY);
			mainwindow_draw_map(DRAWFLAG_ALL);
		}
		else {
			mainwindow_draw_map(DRAWFLAG_GEOMETRY);
		}
	}
	return TRUE;
}

void mainwindow_map_center_on_windowpoint(gint nX, gint nY)
{
	// Calculate the # of pixels away from the center point the click was
	gint16 nPixelDeltaX = nX - (GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.width / 2);
	gint16 nPixelDeltaY = nY - (GTK_WIDGET(g_MainWindow.m_pDrawingArea)->allocation.height / 2);

	// Convert pixels to world coordinates
	gint nZoomLevel = map_get_zoomlevel(g_MainWindow.m_pMap);
	double fWorldDeltaX = map_pixels_to_degrees(g_MainWindow.m_pMap, nPixelDeltaX, nZoomLevel);
	// reverse the X, clicking above
	double fWorldDeltaY = -map_pixels_to_degrees(g_MainWindow.m_pMap, nPixelDeltaY, nZoomLevel);

	mappoint_t pt;
	map_get_centerpoint(g_MainWindow.m_pMap, &pt);

	pt.m_fLatitude += fWorldDeltaY;
	pt.m_fLongitude += fWorldDeltaX;
	map_set_centerpoint(g_MainWindow.m_pMap, &pt);

	mainwindow_statusbar_update_position();
}

void mainwindow_map_center_on_mappoint(mappoint_t* pPoint)
{
	g_assert(pPoint != NULL);

	map_set_centerpoint(g_MainWindow.m_pMap, pPoint);

	mainwindow_statusbar_update_position();
}

void mainwindow_map_slide_to_mappoint(mappoint_t* pPoint)
{
	mappoint_t centerPoint;
	map_get_centerpoint(g_MainWindow.m_pMap, &centerPoint);

	if(map_points_equal(pPoint, &centerPoint)) return;

	if(map_get_distance_in_pixels(g_MainWindow.m_pMap, pPoint, &centerPoint) < MAX_DISTANCE_FOR_AUTO_SLIDE_IN_PIXELS) {
		g_MainWindow.m_bSliding = TRUE;
		g_MainWindow.m_pAnimator = animator_new(ANIMATIONTYPE_FAST_THEN_SLIDE, SLIDE_TIME_IN_SECONDS_AUTO);

		// set startpoint
		g_MainWindow.m_ptSlideStartLocation.m_fLatitude = centerPoint.m_fLatitude;
		g_MainWindow.m_ptSlideStartLocation.m_fLongitude = centerPoint.m_fLongitude;

		// set endpoint
		g_MainWindow.m_ptSlideEndLocation.m_fLatitude = pPoint->m_fLatitude;
		g_MainWindow.m_ptSlideEndLocation.m_fLongitude = pPoint->m_fLongitude;

		//mainwindow_callback_on_slide_timeout(NULL);
	}
	else {
		mainwindow_map_center_on_mappoint(pPoint);
		
		// XXX: this is kind of a hack, but the caller can't add a history item because it might be a slide... hmm
		mainwindow_add_history();
	}
}

void mainwindow_get_centerpoint(mappoint_t* pPoint)
{
	g_assert(pPoint != NULL);

	map_get_centerpoint(g_MainWindow.m_pMap, pPoint);
}

void mainwindow_on_addpointmenuitem_activate(GtkWidget *_unused, gpointer* __unused)
{
/*
	mappoint_t point;
	map_windowpoint_to_mappoint(g_MainWindow.m_pMap, &g_MainWindow.m_ptClickLocation, &point);

	gint nLocationSetID = 1;
	gint nNewLocationID;

	if(locationset_add_location(nLocationSetID, &point, &nNewLocationID)) {
		g_print("new location ID = %d\n", nNewLocationID);
		mainwindow_draw_map(DRAWFLAG_ALL);
	}
	else {
		g_print("insert failed\n");
	}
*/
}

void mainwindow_sidebar_set_tab(gint nTab)
{
	gtk_notebook_set_current_page(g_MainWindow.m_pSidebarNotebook, nTab);
}

//
// History (forward / back buttons)
//
void mainwindow_update_forward_back_buttons()
{
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pForwardButton), history_can_go_forward(g_MainWindow.m_pHistory));
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pBackButton), history_can_go_back(g_MainWindow.m_pHistory));

	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pForwardMenuItem), history_can_go_forward(g_MainWindow.m_pHistory));
	gtk_widget_set_sensitive(GTK_WIDGET(g_MainWindow.m_pBackMenuItem), history_can_go_back(g_MainWindow.m_pHistory));
}

void mainwindow_go_to_current_history_item()
{
	mappoint_t point;
	gint nZoomLevel;
	history_get_current(g_MainWindow.m_pHistory, &point, &nZoomLevel);

	mainwindow_set_zoomlevel(nZoomLevel);
	mainwindow_map_center_on_mappoint(&point);
	mainwindow_draw_map(DRAWFLAG_ALL);
//	mainwindow_draw_map(DRAWFLAG_GEOMETRY);
}

void mainwindow_on_backmenuitem_activate(GtkWidget* _unused, gpointer* __unused)
{
	history_go_back(g_MainWindow.m_pHistory);
	mainwindow_go_to_current_history_item();
	mainwindow_update_forward_back_buttons();
}

void mainwindow_on_forwardmenuitem_activate(GtkWidget* _unused, gpointer* __unused)
{
	history_go_forward(g_MainWindow.m_pHistory);
	mainwindow_go_to_current_history_item();
	mainwindow_update_forward_back_buttons();
}

void mainwindow_on_backbutton_clicked(GtkWidget* _unused, gpointer* __unused)
{
	history_go_back(g_MainWindow.m_pHistory);
	mainwindow_go_to_current_history_item();
	mainwindow_update_forward_back_buttons();
}

void mainwindow_on_forwardbutton_clicked(GtkWidget* _unused, gpointer* __unused)
{
	history_go_forward(g_MainWindow.m_pHistory);
	mainwindow_go_to_current_history_item();
	mainwindow_update_forward_back_buttons();
}

void mainwindow_add_history()
{
	// add the current spot to the history
	mappoint_t point;

	map_get_centerpoint(g_MainWindow.m_pMap, &point);
	history_add(g_MainWindow.m_pHistory, &point, map_get_zoomlevel(g_MainWindow.m_pMap));

	mainwindow_update_forward_back_buttons();
}

static void mainwindow_on_locationset_visible_checkbox_clicked(GtkCellRendererToggle *pCell, gchar *pszPath, gpointer pData)
{
	// get an iterator for this item
	GtkTreePath *pPath = gtk_tree_path_new_from_string(pszPath);
	GtkTreeIter iter;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(g_MainWindow.m_pLocationSetsListStore), &iter, pPath);
	gtk_tree_path_free (pPath);

	// get locationset ID and whether it's set or not
	gboolean bEnabled;
	gint nLocationSetID;
	gtk_tree_model_get(GTK_TREE_MODEL(g_MainWindow.m_pLocationSetsListStore), &iter, LOCATIONSETLIST_COLUMN_ENABLED, &bEnabled, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(g_MainWindow.m_pLocationSetsListStore), &iter, LOCATIONSETLIST_COLUMN_ID, &nLocationSetID, -1);

	// toggle selection and set it
	bEnabled = !bEnabled;
	gtk_list_store_set(g_MainWindow.m_pLocationSetsListStore, &iter, LOCATIONSETLIST_COLUMN_ENABLED, bEnabled, -1);

	locationset_t* pLocationSet = NULL;
	if(locationset_find_by_id(nLocationSetID, &pLocationSet)) {
		locationset_set_visible(pLocationSet, bEnabled);
	}
	else {
		g_assert_not_reached();
	}

	GTK_PROCESS_MAINLOOP;

	mainwindow_draw_map(DRAWFLAG_ALL);
}

#ifdef ROADSTER_DEAD_CODE
/*
static gboolean on_searchbox_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	// Enter key?
	if(event->keyval == 65293) {
		// Grab the first MAX_SEARCH_TEXT_LENGTH characters from search box
		gchar* pchSearchString = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, MAX_SEARCH_TEXT_LENGTH);
		search_road_execute(pchSearchString);
		g_free(pchSearchString);
	}
	return FALSE;
}
*/
#endif /* ROADSTER_DEAD_CODE */
