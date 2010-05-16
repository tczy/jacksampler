AC_INIT([jacksampler], [0.0.1])
#AC_CONFIG_SRCDIR([src/smenc.cc])
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

# We need pkg-config
AC_CHECK_PROG(HAVE_PKG_CONFIG,pkg-config,yes,no)
if test "$HAVE_PKG_CONFIG" == "no"; then
  echo "You need to have pkg-config installed to build this package"
  exit 1
fi

# Check requirement sets
AC_BSE_REQUIREMENTS
AC_JACK_REQUIREMENTS

AC_CONFIG_FILES([Makefile src/Makefile docs/Makefile])
AC_OUTPUT
