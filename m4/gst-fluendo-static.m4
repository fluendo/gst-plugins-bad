dnl Simple macro to define or not a static plugin
dnl use: AG_GST_FLUENDO_STATIC(name, description)

AC_DEFUN([AG_GST_FLUENDO_STATIC],
[
m4_pushdef([UP], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWN], m4_translit([$1], [-A-Z], [_a-z]))dnl

AC_ARG_ENABLE([static-plugin-$1],
   [AC_HELP_STRING([--enable-static-plugin-$1], [enable build of $2])],
   [
    if test "x${enableval}" = "xyes" ; then
       enable_plugin="yes"
    else
       enable_plugin="no"
    fi
   ],
   [enable_plugin="no"])

AC_MSG_CHECKING([whether to enable $2 built])
AC_MSG_RESULT([${enable_plugin}])

AM_CONDITIONAL(FLUENDO_STATIC_[]UP, [test "x${enable_plugin}" = "xyes"])

m4_popdef([UP])
m4_popdef([DOWN])
])
