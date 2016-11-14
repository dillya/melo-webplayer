dnl Autoconf settings for Melo-WebPlayer
AC_PREREQ([2.69])

dnl Initialize Autoconf for Melo-WebPlayer
AC_INIT([Melo-WebPlayer], [0.0.1], [http://www.sparod.com/melo/], [melo-webplayer])

dnl Initialize Automake
AM_INIT_AUTOMAKE([subdir-objects])

dnl Set up macro directory
AC_CONFIG_MACRO_DIR([m4])

dnl Set source directory
AC_CONFIG_SRCDIR([src/melo_webplayer_plugin.c])

dnl Check for programs
AC_PROG_CC
AM_PROG_CC_C_O

dnl Check and init libtool
LT_PREREQ([2.2.6])
LT_INIT([disable-static])

dnl Check for header files
AC_HEADER_STDC

dnl Check for Libmelo
LIBMELO_REQ=0.0.1
PKG_CHECK_MODULES([LIBMELO],
  libmelo >= $LIBMELO_REQ, [],
  [
    AC_MSG_ERROR(Libmelo $LIBMELO_REQ or later is required)
  ])

dnl Set Melo libraries directory
melolibdir="\$(libdir)/melo"
AC_SUBST(melolibdir)

AC_CONFIG_FILES(
Makefile
src/Makefile
)
AC_OUTPUT
