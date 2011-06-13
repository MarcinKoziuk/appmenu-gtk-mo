AC_PREREQ(2.59)

AC_INIT([appmenu-gtk], [0.2.1], [crussell@canonical.com])
AC_COPYRIGHT([Copyright 2010 Canonical])
AC_CONFIG_SRCDIR([src/bridge.c])
AC_CONFIG_MACRO_DIR([build/autotools])

AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([1.9])

AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_C_O
AC_STDC_HEADERS
AC_PROG_LIBTOOL

AC_SUBST(VERSION)

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

###########################
# Dependencies - GLib
###########################

GTK_REQUIRED_VERSION=2.18
DBUSMENU_REQUIRED_VERSION=0.3.101

PKG_CHECK_MODULES(APPMENU, gtk+-2.0 >= $GTK_REQUIRED_VERSION
                           dbusmenu-gtk-0.4 >= $DBUSMENU_REQUIRED_VERSION)

AC_SUBST(APPMENU_CFLAGS)
AC_SUBST(APPMENU_LIBS)

###########################
# Files
###########################

SHAVE_INIT([build/autotools], [enable])

AC_CONFIG_FILES([
Makefile
build/Makefile
build/autotools/Makefile
build/autotools/shave-libtool
build/autotools/shave
src/Makefile
])

AC_OUTPUT

###########################
# Results
###########################

AC_MSG_NOTICE([

appmenu-gtk configuration:

	Prefix:                 $prefix
])

