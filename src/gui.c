/***************************************************************************
 *            gui.c
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

#include <glade/glade.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>

#include "../include/gui.h"
#include "../include/map.h"
#include "../include/util.h"
#include "../include/db.h"

#include "../include/mainwindow.h"
#include "../include/gotowindow.h"
#include "../include/importwindow.h"
#include "../include/datasetwindow.h"
#include "../include/welcomewindow.h"
#include "../include/searchwindow.h"
#include "../include/databasewindow.h"

void gui_init()
{
	GladeXML *pGladeXML;

	// Load glade UI definition file and connect to callback functions	
	pGladeXML = glade_xml_new (PACKAGE_DATA_DIR"/roadster/glade/roadster.glade", NULL, NULL);
	if(pGladeXML == NULL) {
		// try source directory if user hasn't done a 'make install' (good for development, too!)
		pGladeXML = glade_xml_new (PACKAGE_SOURCE_DIR"/roadster.glade", NULL, NULL);

		if(pGladeXML == NULL) {
			g_message("can't find glade xml file");
			gtk_exit(0);
		}
	}
	glade_xml_signal_autoconnect(pGladeXML);

	// init all windows/dialogs
	mainwindow_init(pGladeXML);	
	searchwindow_init(pGladeXML);
	gotowindow_init(pGladeXML);
	importwindow_init(pGladeXML);
	datasetwindow_init(pGladeXML);
	welcomewindow_init(pGladeXML);
	databasewindow_init(pGladeXML);
}

void gui_run()
{
	if(databasewindow_connect()) {
		db_create_tables();

		if(db_is_empty()) {
			welcomewindow_show();
		}
		else {
			mainwindow_show();
		}
		gtk_main();
	}
	else {
		return;
	}
}

void gui_exit()
{
	// Hide first, then quit (makes the UI feel snappier)
	mainwindow_hide();
	gotowindow_hide();

	gtk_exit(0);
}
