#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="roadster"

(test -f $srcdir/configure.ac) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common from the GNOME CVS"
    exit 1
}

ACLOCAL_FLAGS="-I macros" \
USE_COMMON_DOC_BUILD=yes \
REQUIRED_AUTOMAKE_VERSION=1.7 \
USE_GNOME2_MACROS=1 \
. gnome-autogen.sh
