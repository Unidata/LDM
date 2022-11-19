dnl Process this file with automake to produce a configure script
dnl Requires ./aclocal.m4
dnl
AC_PREREQ([2.69])
AC_INIT([LDM],[6.14.1.1],[support-ldm@unidata.ucar.edu],[],[http://www.unidata.ucar.edu/software/ldm])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects no-installinfo])
AC_CONFIG_HEADERS([config.h])

AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug],
	[enable support for debugging [[default=disabled]]]),
    [
	case "$enableval" in
	(yes) enable_debug=true
	     # The ": CFLAGS=..." form doesn't work. Bad colon!
	     # CFLAGS and CXXFLAGS are automatically substituted
	     CFLAGS="${CFLAGS:+$CFLAGS }-g -O0"
	     CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-g -O0"
            ;;
	(no) enable_debug=false
            AC_DEFINE([NDEBUG], , [whether or not to disable assertions])
            ;;
	(*) AC_MSG_ERROR(
	    [bad value {$enableval} for --enable-debug])
	    ;;
	esac
    ],
    [
	enable_debug=false
        AC_DEFINE([NDEBUG], , [whether or not to disable assertions])
    ])

if test -z "$CC" -a `uname -s` = "SunOS"; then
    CC=c99
fi
AC_PROG_CC()
if test "$ac_cv_prog_cc_c99" -a "$ac_cv_prog_cc_c99" = "no"; then
    AC_MSG_ERROR([No c99-conforming compiler found. Install one (e.g., gcc) and retry.])
fi
if test "$GCC" = "yes"; then
    AM_CFLAGS="${AM_CFLAGS:+$AM_CFLAGS }-std=c99"
fi
# "AM_CFLAGS" is not automatically substituted
AC_SUBST([AM_CFLAGS])

# NB: The Solaris 10 compiler might not support _XOPEN_SOURCE beyond 600
if test `uname -s` = "SunOS"; then
    AC_DEFINE([_XOPEN_SOURCE], [600], [Level of X/OPEN compliance])
else
    AC_DEFINE([_XOPEN_SOURCE], [700], [Level of X/OPEN compliance])
fi
# The following always defines _GNU_SOURCE which conflicts with _XOPEN_SOURCE=600
#AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CXX
if test "$GXX" = "yes"; then
    AM_CXXFLAGS="${AM_CXXFLAGS:+$AM_CXXFLAGS }-std=c++11"
fi
# "AM_CXXFLAGS" is not automatically substituted
AC_SUBST([AM_CXXFLAGS])

#if test "$GXX" = "yes"; then
    #case `$CXX -dumpversion` in
        #([5-9].*|4.8.*) DISTCHECK_CONFIGURE_FLAGS_WITH_MULTICAST=--with-multicast;;
        #(4.[0-7]*|[1-3].*) DISTCHECK_CONFIGURE_FLAGS_WITH_MULTICAST=;;
    #esac
#else
    #DISTCHECK_CONFIGURE_FLAGS_WITH_MULTICAST=
#fi
#AC_SUBST([DISTCHECK_CONFIGURE_FLAGS_WITH_MULTICAST])
case "`uname -sr`" in
    ("SunOS 5."*) AC_DEFINE([__EXTENSIONS__], [1], [Needed for "struct ip_mreq" on SunOS 5]);;
    (Darwin*)     AC_DEFINE([_DARWIN_C_SOURCE], [1], [Needed for "struct ip_mreq on Darwin"]);;
    (FreeBSD*)    AC_DEFINE([__BSD_VISIBLE], [1], [Needed on FreeBSD systems]);;
esac

YEAR=`date +%Y`
AC_SUBST([YEAR])

#UD_CPPFLAGS
#AC_DEFINE([_POSIX_C_SOURCE], [1], [Level of POSIX compliance])
AC_DEFINE([CONFIG_H], [1], [Header-file lockout])
#AC_DEFINE([_ALL_SOURCE], [1], [Additional POSIX functionality under AIX])

LT_INIT
AM_PROG_CC_C_O
if test -z "$CC"; then
    AC_MSG_ERROR([no C compiler found])
else
    AC_MSG_CHECKING(the C compiler)
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int foo;])], 
	AC_MSG_RESULT(works),
	AC_MSG_FAILURE($CC failed to compile test code))
fi

case "$CC" in
(*c[[89]]9*)
    ARFLAGS=-cru
    if test "$max_size" = yes
    then
        AC_MSG_NOTICE([checking how to maximize the (long, void*, off_t) 
                programming environment])
        success=no
        AC_MSG_CHECKING([if the system supports _XBS5_LPBIG_OFFBIG])
        avail=`getconf _XBS5_LPBIG_OFFBIG 2>/dev/null`
        if test $? = 0 -a "$avail" \!= -1 -a "$avail" \!= "undefined"
        then
            if cflags=`getconf XBS5_LPBIG_OFFBIG_CFLAGS 2>/dev/null`
            then
                if ldflags=`getconf XBS5_LPBIG_OFFBIG_LDFLAGS 2>/dev/null`
                then
                    if libs=`getconf XBS5_LPBIG_OFFBIG_LIBS 2>/dev/null`
                    then
                        success=yes
                    fi
                fi
            fi
        fi
        AC_MSG_RESULT($success)
        if test "$success" = no
        then
            AC_MSG_CHECKING([if the system supports _XBS5_LP64_OFF64])
            avail=`getconf _XBS5_LP64_OFF64 2>/dev/null`
            if test $? = 0 -a "$avail" \!= -1 -a "$avail" \!= "undefined"
            then
                if cflags=`getconf XBS5_LP64_OFF64_CFLAGS 2>/dev/null`
                then
                    if ldflags=`getconf XBS5_LP64_OFF64_LDFLAGS 2>/dev/null`
                    then
                        if libs=`getconf XBS5_LP64_OFF64_LIBS 2>/dev/null`
                        then
                            success=yes
                        fi
                    fi
                fi
            fi
            AC_MSG_RESULT($success)
        fi
        if test "$success" = no
        then
            AC_MSG_CHECKING([if the system supports _XBS5_ILP32_OFFBIG])
            avail=`getconf _XBS5_ILP32_OFFBIG 2>/dev/null`
            if test $? = 0 -a "$avail" \!= -1 -a "$avail" \!= "undefined"
            then
                if cflags=`getconf XBS5_ILP32_OFFBIG_CFLAGS 2>/dev/null`
                then
                    if ldflags=`getconf XBS5_ILP32_OFFBIG_LDFLAGS 2>/dev/null`
                    then
                        if libs=`getconf XBS5_ILP32_OFFBIG_LIBS 2>/dev/null`
                        then
                            success=yes
                        fi
                    fi
                fi
            fi
            AC_MSG_RESULT($success)
        fi
        if test "$success" = yes
        then
            test -n "$cflags"  && CFLAGS="$CFLAGS${CFLAGS+ }$cflags"
            test -n "$ldflags" && LDFLAGS="$LDFLAGS${LDFLAGS+ }$ldflags"
            test -n "$libs"    && LIBS="$LIBS${LIBS+ }$libs"
            test `uname` = AIX && ARFLAGS='-cru -X 32_64'
            AC_MSG_NOTICE([cflags="'$cflags'"])
            AC_MSG_NOTICE([ldflags="'$ldflags'"])
            AC_MSG_NOTICE([libs="'$libs'"])
            AC_MSG_NOTICE([ARFLAGS="$ARFLAGS"])
            AC_MSG_CHECKING([the C compiler (again)])
            AC_COMPILE_IFELSE([AC_LANG_SOURCE([int foo;])], 
                AC_MSG_RESULT(works),
                AC_MSG_FAILURE($CC failed to compile test code))
        fi
    fi
    AC_SUBST(ARFLAGS);;
esac

case `uname -s` in
    (OSF1)
        AC_DEFINE([_OSF_SOURCE], [1],
                [Needed for "struct mreq" on OSF1 systems])
        ;;
    (IRIX*)
        AC_DEFINE([_SGI_SOURCE], [1],
                [Needed for "struct mreq" on IRIX systems])
        AC_DEFINE([_BSD_TYPES], [1],
                [Needed for "u_int" and the like on IRIX systems])
        ;;
esac

AM_CONDITIONAL([MAINTAINER], [test -d .git])

# Whether to use the original ulog(3) library for logging
AC_ARG_WITH([ulog],
    AS_HELP_STRING([--with-ulog],
	[use the original ulog(3) logging library]
        [[default=no]]),
    [],
    [with_ulog=no])
AM_CONDITIONAL([WANT_ULOG], [test "x$with_ulog" = xyes])
AM_CONDITIONAL([WANT_SLOG], [test "x$with_ulog" != xyes])
AM_COND_IF([WANT_ULOG], [AC_DEFINE([WANT_ULOG], [1],
    [Package will use ulog(3) for logging])])
AM_COND_IF([WANT_SLOG], [AC_DEFINE([WANT_SLOG], [1],
    [Package will use slog(3) for logging])])

# The following is for multicast data distribution using the Virtual Circuit
# Multicast Transport Protocol. DO NOT USE!
#AC_ARG_WITH([multicast],
    #AS_HELP_STRING([--with-multicast],
	#[enable support for multicast. DO NOT USE!
        #[default=without]]),
    #[],
    #[with_multicast=no])
#AM_CONDITIONAL([WANT_MULTICAST], [test "x$with_multicast" != xno])
AM_CONDITIONAL([WANT_MULTICAST], [false])
#AM_COND_IF([WANT_MULTICAST], [
    #AC_DEFINE([WANT_MULTICAST], [1], [Package will have multicast capability])
#])
AC_DEFINE([WANT_MULTICAST], [0], [Package will have multicast capability])

AC_ARG_WITH([noaaport],
    AS_HELP_STRING([--with-noaaport],
	[enable support for reading data from a NOAAPORT receiver
        [default=without]]),
    [],
    [with_noaaport=no])
AM_CONDITIONAL([WANT_NOAAPORT], [test "x$with_noaaport" != xno])

AC_ARG_WITH([ingester],
    AS_HELP_STRING([--with-ingester],
	[enable support for ingesting files into product queue
        [default=without]]),
    [],
    [with_ingester=no])
AM_CONDITIONAL([WANT_INGESTER], [test "x$with_ingester" != xno])

AC_ARG_WITH([retrans],
    AS_HELP_STRING([--with-retrans],
        [enable support for retransmission from a NOAAPORT receiver
        [default=without]]),
    [],
    [with_retrans=no])
AM_CONDITIONAL([WANT_RETRANS], [test "x$with_retrans" != xno])

AC_ARG_WITH([gribinsert],
    AS_HELP_STRING([--with-gribinsert],
	[enable support for converting GRIB2 files into data-products
        [default=without]]),
    [],
    [with_gribinsert=no])
AM_CONDITIONAL([WANT_GRIBINSERT], [test "x$with_gribinsert" != xno])

# Whether or not to include the delayQueue(1) utility in the build
AC_ARG_WITH([delayQueue],
    AS_HELP_STRING([--with-delayQueue],
	[enable building the delayQueue(1) utility
        [default=without]]),
    [],
    [with_delayQueue=no])
AM_CONDITIONAL([WANT_DELAYQUEUE], [test "x$with_delayQueue" != xno])

AC_ARG_ENABLE(faux-pq,
    AS_HELP_STRING([--enable-faux-pq],
	[use the faux product-queue [[default=disable]]]),
    [
	case "${enableval}" in
	(yes) fauxPq=yes;;
	(no)  fauxPq=no;;
	(*)   AC_MSG_ERROR([bad value ${enableval} for --enable-faux-pq]);;
	esac
    ],
    [
	fauxPq=no
    ])
AM_CONDITIONAL([WANT_FAUX_PQ], [test $fauxPq = "yes"])
AM_COND_IF([WANT_FAUX_PQ],
    [PQ_PATH="\$etc_path/pq.xml"
    PQ_SUBDIR=fauxPq],
    [PQ_PATH="\$data_path/ldm.pq"
    PQ_SUBDIR=pq])
AC_SUBST(PQ_PATH)
AC_SUBST(PQ_SUBDIR)

AC_ARG_ENABLE(logging,
    AS_HELP_STRING([--enable-logging=facility],
	[use "facility" when logging [[default=local0]]]),
    [
	facility=`echo $enableval |
	    tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ`
	AC_DEFINE_UNQUOTED([LOG_LDM], [LOG_$facility],
	    [Default LDM logging facility [LOG_LOCAL0]])
	AC_SUBST(LOG_LDM, $enableval)
    ],
    [
	AC_DEFINE_UNQUOTED(LOG_LDM, LOG_LOCAL0,
	    [Default LDM logging facility [LOG_LOCAL0]])
	AC_SUBST(LOG_LDM, local0)
    ])

AC_ARG_ENABLE(port,
    AS_HELP_STRING([--enable-port=port],
	[use "port" for LDM server [[default=388]]]),
    [
	LDM_PORT=$enableval
	ldm_prog=300029
    ],
    [
	LDM_PORT=388
	ldm_prog=300029
    ])
AC_DEFINE_UNQUOTED(LDM_PORT, $LDM_PORT, [Port on which LDM server listens])
AC_DEFINE_UNQUOTED(LDM_PROG, $ldm_prog, [RPC program-number of LDM server])
AC_DEFINE([RESOLVER_TIME_THRESHOLD], [10], [Time limit for resolving hostname])
AC_SUBST(LDM_PORT)

AC_ARG_ENABLE(max-size,
    AS_HELP_STRING([--disable-max-size], 
	[minimize the (long, void*, off_t) programming environment
	[[default=enable]]]),
    max_size=$enableval,
    max_size=yes)

AC_ARG_ENABLE([setpgid-exec],
    AS_HELP_STRING([--disable-exec-setpgid], 
	[disable making pqact(1) EXEC programs process-group leaders so that
	they don't receive signals sent to the LDM process group
	[[default=enable]]]),
    [test "$enableval" = yes && AC_DEFINE([WANT_SETPGID_EXEC])],
    AC_DEFINE([WANT_SETPGID_EXEC], [1],
        [Make pqact(1) EXEC programs process-group leaders]))

# Check for the CUNIT unit-testing package
AC_MSG_NOTICE([checking for the CUnit unit-testing package.])
CPPFLAGS_CUNIT=
AC_CHECK_FILE([/opt/include/CUnit/CUnit.h],
    [CPPFLAGS_CUNIT=-I/opt/include],
    [AC_CHECK_FILE([/usr/local/include/CUnit/CUnit.h],
        [CPPFLAGS_CUNIT=-I/usr/local/include],
        [AC_CHECK_FILE([/usr/include/CUnit/CUnit.h],
            [CPPFLAGS_CUNIT=-I/usr/include])
        ])
    ])
AC_SUBST([CPPFLAGS_CUNIT])
LIBS_CUNIT=
AC_CHECK_FILE([/opt/lib/libcunit.so],
    [ LIBS_CUNIT='-L/opt/lib -lcunit' ],
    [ AC_CHECK_FILE([/opt/lib/libcunit.a],
        [ LIBS_CUNIT='-L/opt/lib -lcunit' ],
        [ AC_CHECK_FILE([/usr/local/lib/libcunit.so],
            [ LIBS_CUNIT='-L/usr/local/lib -lcunit' ],
            [ AC_CHECK_FILE([/usr/local/lib/libcunit.a],
                [ LIBS_CUNIT='-L/usr/local/lib -lcunit' ],
                [ AC_CHECK_FILE([/usr/lib64/libcunit.so],
                    [ LIBS_CUNIT='-L/usr/lib64 -lcunit' ],
                    [ AC_CHECK_FILE([/usr/lib64/libcunit.a],
                        [ LIBS_CUNIT='-L/usr/lib64 -lcunit' ],
                        [ AC_CHECK_FILE([/usr/lib/libcunit.so],
                            [ LIBS_CUNIT='-L/usr/lib -lcunit' ],
                            [ AC_CHECK_FILE([/usr/lib/libcunit.a],
                                [ LIBS_CUNIT='-L/usr/lib -lcunit' ])
                            ])
                        ])
                    ])
                ])
            ])
        ])
    ])
AC_SUBST([LIBS_CUNIT])
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

# Check for the Google Test package
AC_MSG_NOTICE([checking for the GTEST unit-testing package.])
AC_CHECK_FILE(
    [/usr/include/gtest/gtest.h], 
    [GTEST_LDADD='-lgtest'
        AC_SUBST([GTEST_LDADD])])
AM_CONDITIONAL([HAVE_GTEST], [test "$GTEST_LDADD"])
AM_COND_IF([HAVE_GTEST], 
    [AC_MSG_NOTICE([GTEST found. Enabling tests.])],
    [AC_MSG_NOTICE([GTEST not found. Disabling tests.])])

AC_SUBST(PATH)

AC_MSG_CHECKING([the installation prefix]);
test -z "$prefix" -o "$prefix" = NONE && prefix=`(cd .. && pwd)`
#AM_COND_IF([MAINTAINER], [UD_PREFIX(..)], [UD_PREFIX(.)])
AC_MSG_RESULT([$prefix])
AC_SUBST([EXEC_PREFIX], [$prefix/bin])

AC_MSG_CHECKING([the name of the host])
HOSTNAME=`uname -n`
AC_SUBST([HOSTNAME])
AC_DEFINE_UNQUOTED([HOSTNAME], ["$HOSTNAME"], [Name of the local host])
AC_MSG_RESULT([$HOSTNAME])

AC_ARG_VAR([LDMHOME], [Root directory of the LDM installation])
AC_MSG_CHECKING([LDMHOME]);
AM_COND_IF([MAINTAINER], [LDMHOME=$prefix],
    [test -z "$LDMHOME" && LDMHOME=`cd $prefix/.. && pwd`])
AC_MSG_RESULT([$LDMHOME])
if test -z $LDMHOME; then
    AC_MSG_ERROR([invalid value $LDMHOME for LDMHOME], [1])
fi
AC_SUBST([LDMHOME])
AC_DEFINE_UNQUOTED([LDMHOME], ["$LDMHOME"],
        [Root directory of the LDM installation])
#
# Certain installation directories are changed if they have default values
# because an LDM installation has both version-dependent and version-independent
# installation directories.
test "$sysconfdir" = '${prefix}/etc' && sysconfdir="${LDMHOME}/etc"
AC_SUBST([ETC_DIR], ["$sysconfdir"])
test "$localstatedir" = '${prefix}/var' && localstatedir="${LDMHOME}/var"

AC_DEFINE_UNQUOTED([LDM_BIN_DIR], ["$prefix/bin"],
    [Default pathname of installed-programs directory])
AC_SUBST([DOCDIR_UTILITIES], $docdir/utilities)
AC_SUBST([DOCDIR_LIBRARIES], $docdir/libraries)
AC_SUBST([LOGS_DIR], [$localstatedir/logs])
AC_DEFINE_UNQUOTED([LDM_LOG_DIR], ["$localstatedir/logs"],
    [Default pathname of LDM logging directory])
AC_DEFINE_UNQUOTED([LDM_VAR_RUN_DIR], ["$localstatedir/run"],
    [Default pathname of LDM var/run directory])
AC_SUBST([QUEUE_DIR], [$localstatedir/queues])
AC_DEFINE_UNQUOTED([LDM_QUEUE_DIR], ["$localstatedir/queues"],
    [Default pathname of LDM product-queue directory])
AC_DEFINE([LDM_QUEUE_PATH], [LDM_QUEUE_DIR "/ldm.pq"],
    [Default pathname of LDM product-queue])
AC_SUBST([LDM_QUEUE_PATH], [$localstatedir/queues/ldm.pq])
AC_DEFINE_UNQUOTED([PQACT_CONFIG_PATH], ["$sysconfdir/pqact.conf"],
    [Default pathname of pqact(1) configuration-file])
AC_DEFINE_UNQUOTED([LDM_CONFIG_PATH], ["$sysconfdir/ldmd.conf"],
    [Default pathname of LDM configuration-file])
AC_DEFINE_UNQUOTED([SCOUR_CONFIG_PATH], ["$sysconfdir/scour.conf"],
    [Default pathname of scour(1) configuration-file])
AC_DEFINE_UNQUOTED([SCOUR_EXCLUDE_PATH], ["$sysconfdir/scour_excludes.conf"],
    [Default pathname of file containing directories-to-be-excluded by scour(1)])
AC_DEFINE_UNQUOTED([PQACT_DATA_DIR], ["$localstatedir/data"],
    [Default pathname of pqact(1) data-directory])
AC_DEFINE_UNQUOTED([PQSURF_DATA_DIR], ["$localstatedir/data"],
    [Default pathname of pqsurf(1) data-directory])
AC_DEFINE_UNQUOTED([PQSURF_QUEUE_PATH], [LDM_QUEUE_DIR "/pqsurf.pq"],
    [Default pathname of pqsurf(1) product-queue])
AC_DEFINE_UNQUOTED([PQSURF_CONFIG_PATH], ["$sysconfdir/pqsurf.conf"],
    [Default pathname of pqsurf(1) configuration-file])
# The following accomodates pre-6.9 LDM installations:
if test -d $LDMHOME/data -a ! -h $LDMHOME/data; then
    DATA_DIR=$LDMHOME
else
    DATA_DIR=$localstatedir/data
fi
AC_SUBST([DATA_DIR])

AC_SUBST(ARFLAGS)
AC_REVISION ($Revision: 1.47.18.22 $)
UD_HPUX
UD_PROG_CPP
UD_PROG_PERL
AC_PROG_AWK
UD_PROG_SH
AC_PROG_YACC
AC_CHECK_PROGS([DOXYGEN], [doxygen /Volumes/storage/local/bin/doxygen], [])

# The following is necessary because
#   * NOAA's RHEL-8 installations don't have rpcgen(1) and don't allow it to be
#     installed; and
#   * The LDM won't build on MacOS unless the native rpcgen(1) is used.
AC_ARG_VAR([RPCGEN], [Pathname of rpcgen(1) utility])
AC_CHECK_PROG([RPCGEN], [rpcgen], [rpcgen], [])
if test -z "$RPCGEN"; then
    if uname -s | grep Darwin >/dev/null; then
        AC_MSG_ERROR([Couldn't find necessary rpcgen(1) utility])
    else
        AC_MSG_NOTICE([Will use rpcgen(1) output files supplied by package])
    fi
fi
AM_CONDITIONAL([HAVE_RPCGEN], [test "$RPCGEN"])

UD_PROG_NTPDATE
AC_SUBST([NTPDATE], ["$NTPDATE"])
UD_PROG_NETSTAT
UD_PROG_TOP
UD_MMAP
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int, 4)
AC_CHECK_SIZEOF(long, 4)
AC_CHECK_SIZEOF(float)
AC_CHECK_SIZEOF(double)
AC_CHECK_SIZEOF(uint64_t)
AC_MSG_CHECKING([size of enum])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <stdlib.h>
    #include <stdio.h>
    int main() {
	enum foo { bar };
	FILE* out = fopen("conftestdata", "w");
	(void)fprintf(out, "%lu\n", (unsigned long)sizeof(enum foo));
	exit(0);
    }]])],[
	SIZEOF_ENUM=`cat conftestdata`
	AC_MSG_RESULT($SIZEOF_ENUM)
	AC_DEFINE_UNQUOTED(SIZEOF_ENUM, $SIZEOF_ENUM, [size of an enum])
    ],[AC_MSG_FAILURE(couldn't run test program)
],[])

AC_MSG_CHECKING([size of gid_t])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <stdlib.h>
    #include <stdio.h>
    #include <sys/types.h>
    int main() {
	FILE* out = fopen("conftestdata", "w");
	(void)fprintf(out, "%lu\n", (unsigned long)sizeof(gid_t));
	exit(0);
    }]])],[
	SIZEOF_GID_T=`cat conftestdata`
	AC_MSG_RESULT($SIZEOF_GID_T)
	AC_DEFINE_UNQUOTED(SIZEOF_GID_T, $SIZEOF_GID_T, [size of a gid_t])
    ],[AC_MSG_FAILURE(couldn't run test program)
],[])

AC_DEFINE([IEEE_FLOAT], [1], [use IEEE floating-point in XDR layer])
AC_DEFINE([IEEE_DOUBLE], [1], [use IEEE double-precision in XDR layer])

AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_TYPE_UINT64_T
AC_CHECK_TYPE(ssize_t, int)
AC_CHECK_TYPE(ptrdiff_t, int)
AC_CHECK_FUNCS([timegm])
AC_CHECK_FUNCS([fstatvfs fstatfs])
TYPE_SOCKLEN_T
AC_CHECK_FUNCS([fsync ftruncate memmove memcmp rename strerror waitpid strdup seteuid setenv mmap sigaction])
UD_SIG_ATOMIC_T
AC_C_CONST
AC_CHECK_MEMBERS([struct stat.st_blksize],[AC_DEFINE(HAVE_STRUCT_STAT_ST_BLKSIZE, 1,
			    [Define to 1 if your `struct stat' has `st_blksize'.])])

UD_SYSLOG_PIDFILE
AC_SUBST(CFLAGS)
UD_ULOG
UD_NEILLEY_COMPAT
UD_DB
UD_MAKEWHATIS
AC_SEARCH_LIBS(
    [pthread_key_create],
    [pthread],
    ,
    [AC_MSG_ERROR([Could not find required function pthread_key_create],[1])])
libs=$LIBS
LIBS=
AC_SEARCH_LIBS(
    [dirname],
    [gen],
    [LD_GEN=$LIBS
    AC_SUBST([LD_GEN])
    LIBS=$libs],
    [AC_MSG_ERROR([Could not find required function dirname],[1])])
AC_SEARCH_LIBS([sched_setscheduler], [rt],)

AC_C_BIGENDIAN
AC_CHECK_HEADERS([errno.h stdio.h unistd.h stdlib.h string.h sys/types.h \
        sys/ipc.h sys/shm.h sys/sem.h sys/stat.h sys/wait.h unistd.h])
AC_CHECK_HEADERS([stropts.h], ,
[
    AC_CHECK_HEADERS([sys/ioctl.h], ,
	AC_MSG_ERROR("Missing necessary header-file"))
])

################################################################################
# Libraries, in order, from least dependent to most:

# Ensure a reference to the math library if necessary.
AC_SEARCH_LIBS([log10], [m], , [AC_MSG_ERROR([Library not found])])

# Ensure a reference to the real-time library, if necessary
AC_SEARCH_LIBS([shm_open], [rt], , [AC_MSG_ERROR([Library not found])])

# Ensure references to networking libraries, if necessary.
AC_SEARCH_LIBS([setsockopt], [socket nsl nsl_s], ,
    [AC_MSG_ERROR([Library not found])])
AC_SEARCH_LIBS([inet_addr], [socket nsl nsl_s], ,
    [AC_MSG_ERROR([Library not found])])

# Ensure references to the ZLIB package, which is used by the PNG library and
# by the XML2 library.
AC_CHECK_HEADER([zlib.h], , [AC_MSG_ERROR([Header-file not found])])
AC_SEARCH_LIBS([deflate], [z], , [AC_MSG_ERROR([Library not found])])

#AM_COND_IF([WANT_MULTICAST], [
    ## Ensure references to the YAML package, which is used by the LDM-7
    ## multicast module.
    #AC_CHECK_HEADER([yaml.h], , [AC_MSG_ERROR([Header-file not found])])
    #AC_SEARCH_LIBS([yaml_emitter_dump], [yaml], ,
        #[AC_MSG_ERROR([Library not found])])
#])

AM_COND_IF([WANT_NOAAPORT], [
    if test -z "$CXX"; then
        AC_MSG_ERROR([A C++ compiler is necessary for NOAAPort ingestion and wasn't found])
    fi
    # Ensure references to the PNG package, which is used by the LDM NOAAPORT
    # module.
    AC_CHECK_HEADER([png.h], , [AC_MSG_ERROR([Header-file not found])])
    AC_SEARCH_LIBS([png_create_read_struct], [png], ,
        [AC_MSG_ERROR([Library not found])])
])

AM_COND_IF([WANT_INGESTER],[
    found=yes
    AC_SEARCH_LIBS([clock_gettime], [rt],,
            [AC_MSG_WARN([Could not find required function clock_gettime])
            found=no])
    AC_SEARCH_LIBS([timer_create], [rt],,
                [AC_MSG_WARN([Could not find required function timer_create])
                found=no])
    AC_SEARCH_LIBS([timer_settime], [rt],,
            [AC_MSG_WARN([Could not find required function timer_settime])
            found=no])
    if test "$found" = "no"; then
        AC_MSG_WARN([The fileIngest ingester will not be built])
    else
        libs=$LIBS
    fi
])
AM_CONDITIONAL([WANT_INGESTER], [test "$found" = "yes"])

# Ensure references to the XML2 package, which is used by the LDM registry
# module.
AC_CHECK_HEADER([libxml/parser.h], ,
    [CPPFLAGS="${CPPFLAGS:+$CPPFLAGS }-I/usr/include/libxml2"
    unset ac_cv_header_libxml_parser_h
    AC_CHECK_HEADER([libxml/parser.h], ,
        [AC_MSG_ERROR([Header-file not found])])])
AC_SEARCH_LIBS([xmlInitParser], [xml2], , [AC_MSG_ERROR([Library not found])])

################################################################################

#AC_CHECK_TYPE([u_short],,[AC_DEFINE([u_short], [unsigned short], [rpcgen(1) typedef])])
#AC_CHECK_TYPE([u_int],,[AC_DEFINE([u_int], [unsigned int], [rpcgen(1) typedef])])
#AC_CHECK_TYPE([u_long],,[AC_DEFINE([u_long], [unsigned long], [rpcgen(1) typedef])])
#AC_CHECK_TYPE([caddr_t],,[AC_DEFINE([caddr_t], [void*], [rpcgen(1) typedef])])

# HACKS:
AC_CHECK_DECL([strtok_r], , [AC_DEFINE([_REENTRANT], [1], 
    [For declarations of *_r() functions on our HP-UX/gcc 3.3.2 system])])

AC_MSG_CHECKING([for PATH_MAX])
AC_COMPILE_IFELSE(
    [
        AC_LANG_SOURCE([
            #include <limits.h>
            char path[[PATH_MAX+1]];
        ])
    ],
    AC_MSG_RESULT([found]),
    [
        AC_DEFINE([PATH_MAX], [255], [Maximum length of a pathname])
        AC_MSG_RESULT([not found])
    ]
)

LT_INIT

AC_CONFIG_FILES([
    fauxPq/Makefile
    feedme/Makefile
    fileIngest/Makefile
    delayQueue/Makefile
    gempak/Makefile
    gempak/tables/Makefile
    grib2/Makefile
    gribinsert/Makefile
    html/basics/activating.html
    html/basics/index.html
    html/basics/Makefile
    html/basics/rpm-install-steps.html
    html/basics/source-install-steps.html
    html/basics/what-gets-installed.html
    html/index.html
    html/known-problems.html
    html/Makefile
    html/tutorial/Makefile
    html/workshop/Makefile
    html/workshop/specifics.html
    hupsyslog/Makefile
    ldmd/Makefile
    ldmping/Makefile
    ldmsend/Makefile
    ldm.spec
    lib/Makefile
    Makefile
    misc/Makefile
    log/Makefile
    log/ulog/Makefile
    noaaport/Makefile
    noaaport/retrans/Makefile
    notifyme/Makefile
    pqact/Makefile
    pqcat/Makefile
    pqcheck/Makefile
    pqcopy/Makefile
    pqcreate/Makefile
    pqcreate/pqcreate.1
    pqexpire/Makefile
    pqinglib/Makefile
    pqing/Makefile
    pqinsert/Makefile
    pq/Makefile
    pqmon/Makefile
    pqsend/Makefile
    pqsurf/Makefile
    pqutil/Makefile
    protocol/Makefile
    protocol2/Makefile
    regex/Makefile
    registry/Makefile
    regutil/Makefile
    regutil/regpar.tab
    rpc/Makefile
    rtstats/Makefile
    scour/Makefile
    scour/scour.sh
    scripts/ldmadmin.pl
    scripts/ldmfail
    scripts/Makefile
    scripts/netcheck
    scripts/plotMetrics
    scripts/syscheck
    test/Makefile
    uldbutil/Makefile
    ulogger/Makefile
])
#   mcast_lib/Makefile
#   mcast_lib/ldm7/Makefile
#   mcast_lib/OESS-Client/Makefile
#   mldm_sender/Makefile

dnl The subsystems are unconditionally configured to enable, for example,
dnl "make dist" to work.
#AC_CONFIG_SUBDIRS([mcast_lib/FMTP-LDM7/UnidataFMTP])

export CC

AC_OUTPUT
