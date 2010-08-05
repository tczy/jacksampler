AC_INIT([jacksampler], [0.0.2])
AC_CONFIG_SRCDIR([src/jacksampler.cc])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE
AC_PROG_CXX

AC_DEFUN([AC_BSE_REQUIREMENTS],
[
    dnl We need libbse
    PKG_CHECK_MODULES(BSE, bse)
    AC_SUBST(BSE_CFLAGS)
    AC_SUBST(BSE_LIBS)
])

AC_DEFUN([AC_JACK_REQUIREMENTS],
[
    dnl We need libbse
    PKG_CHECK_MODULES(JACK, [jack])
    AC_SUBST(JACK_CFLAGS)
    AC_SUBST(JACK_LIBS)
])

dnl ----------------------------- from acbirnet.m4 -------------------------------
dnl Check whether cc accepts a certain option
dnl MC_PROG_CC_SUPPORTS_OPTION(OPTIONS, ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND])
AC_DEFUN([MC_PROG_CC_SUPPORTS_OPTION], [
AC_MSG_CHECKING([whether ${CC-cc} supports $1])
echo >conftest.c;
if ${CC-cc} [$1] -c $CFLAGS conftest.c >/dev/null 2>&1 ; then
    AC_MSG_RESULT(yes)
    [$2]
else
    AC_MSG_RESULT(no)
    [$3]
fi
rm -fr conftest*
])dnl

# We need pkg-config
AC_CHECK_PROG(HAVE_PKG_CONFIG,pkg-config,yes,no)
if test "$HAVE_PKG_CONFIG" = "no"; then
  echo "You need to have pkg-config installed to build this package"
  exit 1
fi

# Check requirement sets
AC_BSE_REQUIREMENTS
AC_JACK_REQUIREMENTS

MC_PROG_CC_SUPPORTS_OPTION([-Wall], [
  CFLAGS="$CFLAGS -Wall"
  CXXFLAGS="$CXXFLAGS -Wall"
])

AC_CONFIG_FILES([Makefile src/Makefile docs/Makefile])
AC_OUTPUT
