dnl Process this file with automake to produce a configure script
dnl Requires ./aclocal.m4
dnl
AC_PREREQ(2.59)
AC_INIT([LDM], [6.11.3.4], [support-ldm@unidata.ucar.edu],,
    [http://www.unidata.ucar.edu/software/ldm])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_SRCDIR([config.h.in])
AM_INIT_AUTOMAKE([foreign subdir-objects no-installinfo])
AC_CONFIG_HEADERS([config.h])

YEAR=`date +%Y`
AC_SUBST([YEAR])

#UD_CPPFLAGS
#AC_DEFINE([_POSIX_C_SOURCE], [1], [Level of POSIX compliance])
AC_DEFINE([CONFIG_H], [1], [Header-file lockout])
AC_DEFINE([_XOPEN_SOURCE], [600], [Level of POSIX compliance])
#AC_DEFINE([_ALL_SOURCE], [1], [Additional POSIX functionality under AIX])
AC_USE_SYSTEM_EXTENSIONS

case `uname -s` in
    OSF1)
        AC_DEFINE([_XOPEN_SOURCE], [500],
                [Needed for MAP_FAILED and EOVERFLOW on OSF1 systems])
        AC_DEFINE([_OSF_SOURCE], [1],
                [Needed for "struct mreq" on OSF1 systems])
        ;;
    IRIX*)
        AC_DEFINE([_SGI_SOURCE], [1],
                [Needed for "struct mreq" on IRIX systems])
        AC_DEFINE([_BSD_TYPES], [1],
                [Needed for "u_int" and the like on IRIX systems])
        ;;
esac

AM_CONDITIONAL([MAINTAINER],
    [test "$USER" = steve && test `uname -n` = gilda.unidata.ucar.edu &&
    test `/bin/pwd` = /machine/steve/ldm/src])

AC_ARG_WITH([noaaport],
    AS_HELP_STRING([--with-noaaport],
	[enable support for reading data from a NOAAPORT receiver
        [[default=without]]]),
    [],
    [with_noaaport=no])
AM_CONDITIONAL([WANT_NOAAPORT], [test "x$with_noaaport" != xno])

AC_ARG_WITH([gribinsert],
    AS_HELP_STRING([--with-gribinsert],
	[enable support for converting GRIB2 files into data-products
        [[default=without]]]),
    [],
    [with_gribinsert=no])
AM_CONDITIONAL([WANT_GRIBINSERT], [test "x$with_gribinsert" != xno])

AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug],
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
    AS_HELP_STRING([--enable-faux-pq],
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

AC_ARG_ENABLE([root-actions],
    AS_HELP_STRING([--disable-root-actions],
	[don't include superuser (i.e., "root") actions in "make install"
	[[default=include]]]),
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
    [if touch conftest && chmod u=rwxs conftest &&
	    ls -l conftest | grep -i rws >/dev/null; then \
	AC_CACHE_CHECK([for su(1) that you can use], [ac_cv_path_SU],
	    [AC_PATH_PROGS_FEATURE_CHECK([SU], [su],
		[echo "Enter root's password (or don't)." >/dev/tty
		$ac_path_SU root -c true </dev/tty >/dev/tty 2>&1 &&
                    ac_cv_path_SU=$ac_path_SU
                    ac_path_SU_found=:],
		[AC_MSG_RESULT([not found])
		AC_CACHE_CHECK([for sudo(1) that you can use],
                    [ac_cv_path_SUDO],
		    [AC_PATH_PROGS_FEATURE_CHECK([SUDO], [sudo],
			[echo "Enter your password (or don't)." >/dev/tty
			$ac_path_SUDO true >/dev/tty 2>&1 </dev/tty &&
                            ac_cv_path_SUDO=$ac_path_SUDO
                            ac_path_SUDO_found=:],
			[AC_MSG_RESULT([not found])])])])])
        [echo Continuing... >/dev/tty]
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
AC_MSG_NOTICE([checking for the CUNIT unit-testing package.])
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
    AC_MSG_ERROR([no C compiler found])
else
    AC_MSG_CHECKING(the C compiler)
    AC_COMPILE_IFELSE([int foo;], 
	AC_MSG_RESULT(works),
	AC_MSG_FAILURE($CC failed to compile test code))
fi

case "$CC" in
*c[[89]]9*)
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
            AC_MSG_NOTICE([cflags="\"$cflags\""])
            AC_MSG_NOTICE([ldflags="\"$ldflags\""])
            AC_MSG_NOTICE([libs="\"$libs\""])
            AC_MSG_NOTICE([ARFLAGS="\"$ARFLAGS\""])
            AC_MSG_CHECKING([the C compiler (again)])
            AC_COMPILE_IFELSE([int foo;], 
                AC_MSG_RESULT(works),
                AC_MSG_FAILURE($CC failed to compile test code))
        fi
    fi
    AC_SUBST(ARFLAGS);;
esac

AC_MSG_CHECKING([the installation prefix]);
test -z "$prefix" -o "$prefix" = NONE && prefix=`(cd .. && pwd)`
#AM_COND_IF([MAINTAINER], [UD_PREFIX(..)], [UD_PREFIX(.)])
AC_MSG_RESULT([$prefix])

AC_MSG_CHECKING([the name of the host])
HOSTNAME=`uname -n`
AC_SUBST([HOSTNAME])
AC_MSG_RESULT([$HOSTNAME])

AC_MSG_CHECKING([LDMHOME]);
AM_COND_IF([MAINTAINER], [LDMHOME=$prefix],
    [test -z "$LDMHOME" && LDMHOME=$HOME])
AC_MSG_RESULT([$LDMHOME])
if test -z $LDMHOME; then
    AC_MSG_ERROR([invalid value $LDMHOME for LDMHOME], [1])
fi
AC_SUBST([LDMHOME])
AC_DEFINE_UNQUOTED([LDMHOME], ["$LDMHOME"],
        [Root directory of the LDM installation])
#
# Certain installation directories are changed if they have default values
# because an LDM installation has both version-dependent and
# version-independent installation directories.
test "$sysconfdir" = '${prefix}/etc' && sysconfdir='$(LDMHOME)/etc'
test "$localstatedir" = '${prefix}/var' && localstatedir='$(LDMHOME)/var'
AC_SUBST([REGISTRY_DIR], ['$(sysconfdir)'/registry])
AC_SUBST([LOGS_DIR], ['$(localstatedir)'/logs])
AC_SUBST([QUEUE_DIR], ['$(localstatedir)'/queues])
# The following accomodates pre-6.9 LDM installations:
if test -d $LDMHOME/data -a ! -h $LDMHOME/data; then
    DATA_DIR=$LDMHOME
else
    DATA_DIR='$(localstatedir)'/data
fi
AC_SUBST([DATA_DIR])

AC_SUBST(ARFLAGS)
AC_REVISION ($Revision: 1.47.18.22 $)
UD_HPUX
UD_PROG_CPP
UD_PROG_PERL
UD_PROG_SH
AC_PROG_AWK
AC_PROG_YACC
UD_PROG_RPCGEN
UD_PROG_NTPDATE
UD_PROG_NETSTAT
UD_PROG_TOP
UD_LIB_YACC
UD_MMAP
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
AC_HEADER_STDC
AC_CHECK_HEADERS([errno.h stdio.h unistd.h stdlib.h string.h sys/types.h \
        sys/ipc.h sys/shm.h sys/sem.h sys/stat.h sys/wait.h unistd.h])
AC_CHECK_HEADERS([stropts.h], ,
[
    AC_CHECK_HEADERS([sys/ioctl.h], ,
	AC_MSG_ERROR("Missing necessary header-file"))
])

AC_CHECK_TYPE([u_short],,[AC_DEFINE([u_short], [unsigned short], [rpcgen(1) typedef])])
AC_CHECK_TYPE([u_int],,[AC_DEFINE([u_int], [unsigned int], [rpcgen(1) typedef])])
AC_CHECK_TYPE([u_long],,[AC_DEFINE([u_long], [unsigned long], [rpcgen(1) typedef])])
AC_CHECK_TYPE([caddr_t],,[AC_DEFINE([caddr_t], [void*], [rpcgen(1) typedef])])

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
    
AC_PROG_LIBTOOL

AC_CONFIG_FILES([
    fauxPq/Makefile
    feedme/Makefile
    gempak/Makefile
    gempak/tables/Makefile
    grib2/Makefile
    gribinsert/Makefile
    html/basics/activating.html
    html/basics/index.html
    html/basics/Makefile
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
    macros.make
    Makefile
    misc/Makefile
    noaaport/Makefile
    notifyme/Makefile
    pqact/Makefile
    pqcat/Makefile
    pqcheck/Makefile
    pqcopy/Makefile
    pqcreate/Makefile
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
    regex/Makefile
    registry/Makefile
    regutil/Makefile
    rpc/Makefile
    rtstats/Makefile
    scour/Makefile
    scour/scour
    scripts/ldmadmin
    scripts/ldmfail
    scripts/Makefile
    scripts/netcheck
    scripts/plotMetrics
    scripts/syscheck
    test/Makefile
    uldbutil/Makefile
    ulogger/Makefile
    ulog/Makefile
    zlib/Makefile
])

export CC

AC_CONFIG_SUBDIRS([libpng])
#AC_CONFIG_SUBDIRS([libxml2])

(cd libxml2 && ./configure --prefix=$prefix --disable-ipv6 --without-c14n --without-catalog --without-debug --without-docbook --without-ftp --without-html --without-http --without-iconv --without-iso8859x --without-legacy --without-pattern --without-python --without-reader --without-readline --without-regexps --without-schemas --without-schematron --without-threads --without-valid --without-xinclude --without-xpath --without-xptr --without-zlib CC="$CC")

AC_OUTPUT
