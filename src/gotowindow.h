/***************************************************************************
 *            gotowindow.h
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
 
void gotowindow_init(GladeXML* pGladeXML);
void gotowindow_show(void);
void gotowindow_hide(void);

/* Funky, auto-lookup glade signal handlers.

   XXX: Better would be to hook these up manually, remove these
   declarations, and make the functions static.
*/
void gotowindow_on_knownlocationtreeview_row_activated(GtkTreeView *treeview, GtkTreePath *arg1, GtkTreeViewColumn *arg2, gpointer user_data);
void gotowindow_on_gobutton_clicked(GtkButton *button, gpointer user_data);

