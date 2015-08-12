AC_PREREQ(2.53)
AC_INIT(aqxclient, 0.1, http://www.systemsbiology.org/)
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR(aqxclient.cpp)

AC_PROG_CC
AC_PROG_CXX

PKG_CHECK_MODULES(GOIO, GoIO)

AC_SUBST(GOIO_CFLAGS)
AC_SUBST(GOIO_LIBS)

AC_CONFIG_FILES([
		Makefile
])

AC_OUTPUT

echo " "
AC_MSG_RESULT([Configured to install in: ${prefix}])

