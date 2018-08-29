dnl Process this file with automake to produce a configure script
dnl Requires ./aclocal.m4
dnl
AC_PREREQ(2.59)
m4_include([version.m4])
AC_INIT([FMTP], VERSION_ID, [support-fmtp@unidata.ucar.edu],,
    [http://www.unidata.ucar.edu/software/fmtp])

AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])

AC_USE_SYSTEM_EXTENSIONS

LT_INIT

AC_PROG_LIBTOOL

AC_PROG_CXX
if test "$GXX" = "yes"; then
    AC_SUBST([AM_CXXFLAGS], [-std=c++0x])
fi

# Check for the CUNIT unit-testing package
AC_MSG_NOTICE([checking for the CUnit unit-testing package.])
AC_CHECK_FILE([/opt/include/CUnit/CUnit.h],
    [CPPFLAGS_CUNIT=-I/opt/include
    AC_SUBST(CPPFLAGS_CUNIT)
    AC_CHECK_FILE([/opt/lib/libcunit.so],
        [LIBS_CUNIT='-L/opt/lib -lcunit'
        AC_SUBST(LIBS_CUNIT)],
        [AC_CHECK_FILE([/opt/lib/libcunit.a],
            [LIBS_CUNIT='-L/opt/lib -lcunit'
            AC_SUBST(LIBS_CUNIT)])])],
    [AC_CHECK_FILE([/usr/local/include/CUnit/CUnit.h],
        [CPPFLAGS_CUNIT=-I/usr/local/include
        AC_SUBST(CPPFLAGS_CUNIT)
        AC_CHECK_FILE([/usr/local/lib/libcunit.so],
            [LIBS_CUNIT='-L/usr/local/lib -lcunit'
            AC_SUBST(LIBS_CUNIT)],
            [AC_CHECK_FILE([/usr/local/lib/libcunit.a],
                [LIBS_CUNIT='-L/usr/local/lib -lcunit'
                AC_SUBST(LIBS_CUNIT)])])],
        [AC_CHECK_FILE([/usr/include/CUnit/CUnit.h],
            [CPPFLAGS_CUNIT=-I/usr/include
            AC_SUBST(CPPFLAGS_CUNIT)
            AC_CHECK_FILE([/usr/lib/libcunit.so],
                [LIBS_CUNIT='-L/usr/lib -lcunit'
                AC_SUBST(LIBS_CUNIT)],
                [AC_CHECK_FILE([/usr/lib/libcunit.a],
                    [LIBS_CUNIT='-L/usr/lib -lcunit'
                    AC_SUBST(LIBS_CUNIT)])])],
            [AC_CHECK_FILE([/usr/lib64/libcunit.so],
                [LIBS_CUNIT='-L/usr/lib64 -lcunit'
                AC_SUBST(LIBS_CUNIT)],
                [AC_CHECK_FILE([/usr/lib64/libcunit.a],
                    [LIBS_CUNIT='-L/usr/lib64 -lcunit'
                    AC_SUBST(LIBS_CUNIT)])])])])])
AM_CONDITIONAL([HAVE_CUNIT], [test "$CPPFLAGS_CUNIT" && test "$LIBS_CUNIT"])
AM_COND_IF([HAVE_CUNIT], 
    [AC_MSG_NOTICE([CUnit found.  Enabling unit-tests.])],
    [AC_MSG_NOTICE([CUnit not found.  Disabling unit-tests.])])

# Check for the Opmock unit-testing and mocking package
AC_MSG_NOTICE([checking for the Opmock mocking package.])
AC_CHECK_FILE(
    [/usr/local/include/opmock.h],
    [   OPMOCK_CPPFLAGS=-I/usr/local/include
        AC_SUBST([OPMOCK_CPPFLAGS])
        libs_prev=$LIBS
        LIBS="-L/usr/local/lib64 -lopmock${LIBS:+ $LIBS}"
        AC_LINK_IFELSE(
            [   AC_LANG_PROGRAM(
                    [void opmock_test_suite_reset();],
                    [opmock_test_suite_reset();]) ],
            [   OPMOCK_LDFLAGS='-rpath /usr/local/lib64'
                OPMOCK_LDADD='-L/usr/local/lib64 -lopmock'
                AC_SUBST([OPMOCK_LDFLAGS])
                AC_SUBST([OPMOCK_LDADD]) ])
        LIBS=$libs_prev])
AM_CONDITIONAL([HAVE_OPMOCK], [test "$OPMOCK_LDADD"])
AM_COND_IF([HAVE_OPMOCK], 
    [AC_MSG_NOTICE([Opmock found. Enabling mocking-tests.])],
    [AC_MSG_NOTICE([Opmock not found. Disabling mocking-tests.])])

# Check for the Google-test C++ unit-testing and mocking package
AC_MSG_NOTICE([checking for the Google Test package.])
AC_CHECK_FILE(
    [/usr/include/gtest/gtest.h],
    [GTEST_CPPFLAGS=
     AC_SUBST([GTEST_CPPFLAGS])
     AC_CHECK_FILE(
        [/usr/lib64/libgtest.so],
        [GTEST_LDFLAGS='-rpath /usr/local/lib64'
         GTEST_LDADD='-L/usr/local/lib64 -lgtest'
         AC_SUBST([GTEST_LDFLAGS])
         AC_SUBST([GTEST_LDADD]) ])])
AM_CONDITIONAL([HAVE_GTEST], [test "$GTEST_LDADD"])
AM_COND_IF([HAVE_GTEST], 
    [AC_MSG_NOTICE([Google Test found. Enabling associated tests.])],
    [AC_MSG_NOTICE([Google Test not found. Disabling associated tests.])])

AC_CONFIG_FILES([
    Makefile
    test/Makefile
    test/sender/Makefile
    FMTPv3/Makefile
    FMTPv3/receiver/Makefile
    FMTPv3/sender/Makefile
    FMTPv3/SilenceSuppressor/Makefile
    FMTPv3/RateShaper/Makefile
])

AC_OUTPUT
