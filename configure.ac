dnl Process this file with automake to produce a configure script
dnl Requires ./aclocal.m4
dnl
AC_PREREQ(2.59)
AC_INIT([LDM], [6.8.2.3], [support-ldm@unidata.ucar.edu],,
    [http://www.unidata.ucar.edu/software/ldm])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_SRCDIR([ldmconfig.h.in])
AM_INIT_AUTOMAKE([foreign subdir-objects no-installinfo])
AC_CONFIG_HEADERS([ldmconfig.h])

#UD_CPPFLAGS
#AC_DEFINE([_POSIX_C_SOURCE], [1], [Level of POSIX compliance])
AC_DEFINE([_XOPEN_SOURCE], [500], [Level of POSIX compliance])
AC_DEFINE([_XOPEN_SOURCE_EXTENDED], [1], [Additional POSIX functionality])
AC_USE_SYSTEM_EXTENSIONS

AM_CONDITIONAL([MAINTAINER],
    [test "$USER" = steve && test $PWD = /home/steve/ldm/package/src &&
        test "X$HOST" = "Xgilda"])

AC_ARG_ENABLE([debug],
    AC_HELP_STRING([--enable-debug],
	[enable support for debugging [[default=disabled]]]),
    [
	case "$enableval" in
	yes) enable_debug=true;;
	no) enable_debug=false;;
	*) AC_MSG_ERROR(
	    [bad value {$enableval} for --enable-debug]);;
	esac
    ],
    [
	enable_debug=false
    ])

AC_ARG_ENABLE(faux-pq,
    AC_HELP_STRING([--enable-faux-pq],
	[use the faux product-queue [[default=disable]]]),
    [
	case "${enableval}" in
	yes) fauxPq=yes;;
	no)  fauxPq=no;;
	*)   AC_MSG_ERROR([bad value ${enableval} for --enable-faux-pq]);;
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
    AC_HELP_STRING([--enable-logging=facility],
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
    AC_HELP_STRING([--enable-port=port],
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

AC_ARG_ENABLE([root-actions],
    AC_HELP_STRING([--enable-root-actions],
	[include superuser (i.e., "root") actions in "make install"
	[[default=enabled]]]),
    [
	case "$enableval" in
	yes) enable_include=true;;
	no) enable_include=false;;
	*) AC_MSG_ERROR(
	    [bad value {$enableval} for --enable-root-actions]);;
	esac
    ],
    [
	enable_include=true
    ])
AM_CONDITIONAL([INCLUDE_ROOT_ACTIONS], [test "x$enable_include" = "xtrue"])
AM_COND_IF([INCLUDE_ROOT_ACTIONS], 
    [if touch conftest && chmod u=rws conftest &&
	    ls -l conftest | grep rws >/dev/null; then \
	AC_CACHE_CHECK([for sudo(1) that you can use], [ac_cv_path_SUDO],
	    [AC_PATH_PROGS_FEATURE_CHECK([SUDO], [sudo],
		[echo "Enter the LDM user's password." >/dev/tty
		$ac_path_SUDO true && ac_cv_path_SUDO=$ac_path_SUDO],
		[AC_MSG_RESULT([not found])
		AC_CACHE_CHECK([for su(1) that you can use], [ac_cv_path_SU],
		    [AC_PATH_PROGS_FEATURE_CHECK([SU], [su],
			[echo "Enter root's password." >/dev/tty
			$ac_path_SU -c true >/dev/tty 2>&1 </dev/tty \
			&& ac_cv_path_SU=$ac_path_SU],
			[AC_MSG_RESULT([not found])])])])])
	AC_SUBST([SU], [$ac_cv_path_SU])
	AC_SUBST([SUDO], [$ac_cv_path_SUDO])
	if test -n "$SUDO" -o -n "$SU"; then \
	    include_root_actions=true
	else
	    include_root_actions=false
	fi
    else
	include_root_actions=false
    fi
    rm -f conftest],
    [include_root_actions=false])
AM_CONDITIONAL([INCLUDE_ROOT_ACTIONS],
    [test "$include_root_actions" = true])
AM_CONDITIONAL([HAVE_SUDO], [test -n "$SUDO"])
AM_CONDITIONAL([HAVE_SU], [test -n "$SU"])

# Check for the CUNIT unit-testing package
AC_MSG_NOTICE([Checking for the CUNIT unit-testing package.])
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
                AC_SUBST(LIBS_CUNIT)])])])])
AM_CONDITIONAL([HAVE_CUNIT], [test "$CPPFLAGS_CUNIT" && test "$LIBS_CUNIT"])
AM_COND_IF([HAVE_CUNIT], 
    [AC_MSG_NOTICE([CUNIT found.  Enabling unit-tests.])],
    [AC_MSG_NOTICE([CUNIT not found.  Disabling unit-tests.])])

AC_SUBST(PATH)

AC_PROG_CC(c89 cc gcc)
AM_PROG_CC_C_O
if test -z "$CC"; then
    AC_MSG_ERROR(no C compiler found)
else
    AC_MSG_CHECKING(the C compiler)
    AC_COMPILE_IFELSE([int foo;], 
	AC_MSG_RESULT(works),
	AC_MSG_FAILURE($CC failed to compile test code))
fi

AC_MSG_CHECKING([the installation prefix]);
test -z "$prefix" -o "$prefix" = NONE && prefix=`pwd`
#AM_COND_IF([MAINTAINER], [UD_PREFIX(..)], [UD_PREFIX(.)])
AC_MSG_RESULT([$prefix])

AC_MSG_CHECKING([LDMHOME]);
test -z "$LDMHOME" && LDMHOME=$HOME
AC_SUBST([LDMHOME])
AC_DEFINE_UNQUOTED([LDMHOME], ["$LDMHOME"],
    [Pathname of the home directory of the LDM user])
AM_COND_IF([MAINTAINER], [LDMHOME=$prefix])
AC_MSG_RESULT([$LDMHOME])
sysconfdir=$LDMHOME/etc

AC_SUBST(ARFLAGS)
AC_REVISION ($Revision: 1.47.18.22 $)
UD_HPUX
UD_PROG_CPP
UD_PROG_PERL
UD_PROG_SH
AC_PROG_YACC
UD_PROG_RPCGEN
UD_PROG_NTPDATE
UD_PROG_NETSTAT
UD_PROG_TOP
UD_LIB_YACC
UD_MMAP
AC_CHECK_TYPE([u_short],,[AC_DEFINE([u_short], [unsigned short], [rpcgen(1) typedef])])
AC_CHECK_TYPE([u_int],,[AC_DEFINE([u_int], [unsigned int], [rpcgen(1) typedef])])
AC_CHECK_TYPE([u_long],,[AC_DEFINE([u_long], [unsigned long], [rpcgen(1) typedef])])
AC_CHECK_TYPE([caddr_t],,[AC_DEFINE([caddr_t], [void*], [rpcgen(1) typedef])])
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int, 4)
AC_CHECK_SIZEOF(long, 4)
AC_CHECK_SIZEOF(float)
AC_CHECK_SIZEOF(double)
AC_CHECK_SIZEOF(uint64_t)
AC_MSG_CHECKING([size of enum])
AC_TRY_RUN([
    #include <stdio.h>
    int main() {
	enum foo { bar };
	FILE* out = fopen("conftestdata", "w");
	(void)fprintf(out, "%lu\n", (unsigned long)sizeof(enum foo));
	exit(0);
    }],
    [
	SIZEOF_ENUM=`cat conftestdata`
	AC_MSG_RESULT($SIZEOF_ENUM)
	AC_DEFINE_UNQUOTED(SIZEOF_ENUM, $SIZEOF_ENUM, [size of an enum])
    ],
    AC_MSG_FAILURE([couldn't run test program])
)

AC_MSG_CHECKING([size of gid_t])
AC_TRY_RUN([
    #include <stdio.h>
    #include <sys/types.h>
    int main() {
	FILE* out = fopen("conftestdata", "w");
	(void)fprintf(out, "%lu\n", (unsigned long)sizeof(gid_t));
	exit(0);
    }],
    [
	SIZEOF_GID_T=`cat conftestdata`
	AC_MSG_RESULT($SIZEOF_GID_T)
	AC_DEFINE_UNQUOTED(SIZEOF_GID_T, $SIZEOF_GID_T, [size of a gid_t])
    ],
    AC_MSG_FAILURE([couldn't run test program])
)

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
AC_CHECK_HEADERS([stdlib.h unistd.h])
AC_CHECK_FUNCS([fsync ftruncate memmove memcmp rename strerror waitpid dnl
strdup seteuid setenv mmap sigaction])
UD_SIG_ATOMIC_T
AC_C_CONST
AC_STRUCT_ST_BLKSIZE
UD_SYSLOG_PIDFILE
AC_SUBST(CFLAGS)
UD_NETWORKING
UD_ULOG
UD_NEILLEY_COMPAT
UD_DB
UD_MAKEWHATIS
libs=$LIBS
LIBS=
AC_SEARCH_LIBS(
    [dirname],
    [gen],
    [LD_GEN=$LIBS
    AC_SUBST([LD_GEN])
    LIBS=$libs],
    [AC_MSG_ERROR([Could not find required function dirname],[1])])

AC_C_BIGENDIAN
AC_CHECK_HEADERS([stropts.h], ,
[
    AC_CHECK_HEADERS([sys/ioctl.h], ,
	AC_MSG_ERROR("Missing necessary header-file"))
])
    
AC_PROG_LIBTOOL

AC_CONFIG_FILES([
    Makefile
    rpc/Makefile
    ulog/Makefile
    misc/Makefile
    protocol/Makefile
    pq/Makefile
    fauxPq/Makefile
    pqcreate/Makefile
    pqcheck/Makefile
    pqinsert/Makefile
    pqcat/Makefile
    pqcat/pqcat.1
    pqexpire/Makefile
    pqmon/Makefile
    pqutil/Makefile
    pqsend/Makefile
    pqing/Makefile
    pqact/Makefile
    pqsurf/Makefile
    ldmsend/Makefile
    rtstats/Makefile
    feedme/Makefile
    notifyme/Makefile
    server/Makefile
    ldmping/Makefile
    rdb/Makefile
    scripts/Makefile
    scripts/ldmadmin
    scripts/writeConfiguration.pl
    scripts/ldmfail
    scripts/netcheck
    scripts/syscheck
    scripts/ldmcheck
    scour/Makefile
    scour/scour
    regex/Makefile
    html/Makefile
    html/basics/Makefile
    html/basics/source-install-steps.html
    html/tutorial/Makefile
    test/Makefile
])

AC_CONFIG_SUBDIRS([rdb/tokyocabinet])

AC_OUTPUT
