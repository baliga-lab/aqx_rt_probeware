AC_PREREQ(2.53)
AC_INIT(aqxclient, 0.1, http://www.systemsbiology.org/)
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_SRCDIR(aqxclient.cpp)
AC_REQUIRE_AUX_FILE([tap-driver.sh])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_AWK

PKG_CHECK_MODULES(GOIO, GoIO)
PKG_CHECK_MODULES(NGIO, [libNGIO])

AC_SUBST(GOIO_CFLAGS)
AC_SUBST(GOIO_LIBS)
AC_SUBST(NGIO_CFLAGS)
AC_SUBST(NGIO_LIBS)

AC_CONFIG_FILES([
		Makefile
])

AC_OUTPUT

echo " "
AC_MSG_RESULT([Configured to install in: ${prefix}])
