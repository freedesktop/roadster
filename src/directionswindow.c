/***************************************************************************
 *            directionswindow.c
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
Purpose of directionswindow.c:
 - This is the bar across the top of the map, visible only when user is following generated directions (XXX: unimplemented)
 - Show step-by-step directions with various info for each step
 - Allow user to view each turn by clicking on the line, etc.
*/

#include <gtk/gtk.h>
#include <glib-object.h>
#include <glade/glade.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
