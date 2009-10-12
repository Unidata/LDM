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

AM_CONDITIONAL([MAINTAINER],
    [test "$USER" == steve && hostname | fgrep -q unidata.ucar.edu])

AC_ARG_ENABLE(faux-pq,
    AC_HELP_STRING([--enable-faux-pq],
	[use the faux product-queue [[default=disable]]]),
    [
	fauxPq=yes
	PQ_PATH="\$etc_path/pq.xml"
    ],
    [
	fauxPq=no
	PQ_PATH="\$data_path/ldm.pq"
    ])
AC_SUBST(PQ_PATH)
AM_CONDITIONAL([WANT_FAUX_PQ], [test $fauxPq = "yes"])

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

AC_ARG_ENABLE(max-size,
    AC_HELP_STRING([--disable-max-size], 
	[minimize the (long, void*, off_t) programming environment
	[[default=enable]]]),
    max_size=$enableval,
    max_size=yes)

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

ARFLAGS=-cru
if test "$max_size" = yes
then
    AC_MSG_NOTICE(Checking how to maximize the (long, void*, off_t) programming environment)
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
		    maprgns=-U_MAPRGNS
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
			maprgns=-U_MAPRGNS
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
			AC_DEFINE(_MAPRGNS, 1,
			    [Causes product-queue to memory-map by
			    data-product])
			maprgns=-D_MAPRGNS
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
	AC_MSG_NOTICE([$maprgns])
	AC_MSG_NOTICE([cflags=\"$cflags\"])
	AC_MSG_NOTICE([ldflags=\"$ldflags\"])
	AC_MSG_NOTICE([libs=\"$libs\"])
	AC_MSG_NOTICE([ARFLAGS=\"$ARFLAGS\"])
	AC_MSG_CHECKING([the C compiler (again)])
	AC_COMPILE_IFELSE([int foo;], 
	    AC_MSG_RESULT(works),
	    AC_MSG_FAILURE($CC failed to compile test code))
    fi
fi

AC_MSG_CHECKING([the installation prefix]);
test -z "$prefix" -o "$prefix" = NONE && prefix=`pwd`
#AM_COND_IF([MAINTAINER], [UD_PREFIX(..)], [UD_PREFIX(.)])
AC_MSG_RESULT([$prefix])
datarootdir=$prefix
docdir=$prefix/doc

AC_MSG_CHECKING([LDMHOME]);
test -z "$LDMHOME" && LDMHOME=$HOME
AC_SUBST([LDMHOME])
AC_DEFINE_UNQUOTED([LDMHOME], ["$LDMHOME"],
    [Pathname of the home directory of the LDM user])
AM_COND_IF([MAINTAINER], [LDMHOME=$prefix])
AC_MSG_RESULT([$LDMHOME])
sysconfdir=$LDMHOME/etc

AC_SUBST(ARFLAGS)
UD_CPPFLAGS
AC_REVISION ($Revision: 1.47.18.22 $)
UD_DOMAINNAME
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
AC_CHECK_SIZEOF([int32_t])
AC_CHECK_SIZEOF([int64_t])

AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_CHECK_TYPE(ssize_t, int)
AC_CHECK_TYPE(ptrdiff_t, int)
AC_CHECK_FUNCS(timegm)
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
    html/tutorial/Makefile
])
AC_OUTPUT
