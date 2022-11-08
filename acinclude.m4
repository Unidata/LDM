dnl $Id: aclocal.m4,v 1.27.2.1.2.1.2.1.2.23 2009/08/21 19:58:25 steve Exp $
dnl
dnl These are the local macros used by the ldm4 configure.in
dnl autoconf 1.6
dnl
dnl This is just like AC_CHECK_FUNCS except that the sense is reversed.
dnl
define(diversion_number, divnum)dnl
divert([-1])

AC_DEFUN([UD_REGPAR], [
    AC_DEFINE([REG_$1], ["$2"], [$3])
    AC_SUBST([REG_$1], [$2])
    m4_ifval([$4], [AC_SUBST([REG_$1_DEFAULT], [$4])])
    echo '$2':'$3':'$4' >>regpar.txt
])


AC_DEFUN([UD_PREFIX],
[
    AC_MSG_CHECKING(the installation prefix)
    case "${prefix-}" in
	NONE|'')
	    prefix=`cd $1; pwd`
	    ;;
	/*) ;;
	*)  prefix=`cd $prefix; pwd` ||
	    {
		AC_MSG_ERROR(invalid value for prefix: $prefix)
	    }
	    ;;
    esac
    AC_MSG_RESULT($prefix)
])



dnl Set the C-preprocessor flags as necessary.
dnl
AC_DEFUN([UD_CPPFLAGS],
[
    case `uname -s` in
	AIX)
	    AC_DEFINE([_XOPEN_SOURCE], [500],
		[Defines level of POSIX compliance])
	    AC_DEFINE([_XOPEN_SOURCE_EXTENDED], [1],
		[Obtains extended POSIX API])
dnl	    AC_DEFINE([_ALL_SOURCE], [1],
dnl		[Define to obtain optional system API])
	    AC_DEFINE([BSD], [43], [Define if on a BSD system])
	    ;;
	Darwin)
	    case `uname -r` in
		8*) AC_DEFINE([__DARWIN_UNIX03], [1],
			[Obtains optional system API])
		    ;;
		*)  AC_DEFINE([_DARWIN_C_SOURCE], [1],
			[Define to obtain optional system API])
		    AC_DEFINE([_XOPEN_SOURCE], [500],
			[Defines level of POSIX compliance])
		    AC_DEFINE([_XOPEN_SOURCE_EXTENDED], [1],
			[Obtains extended POSIX API])
		    ;;
	    esac
	    ;;
	HP-UX)
	    AC_DEFINE([_XOPEN_SOURCE], [500],
		[Defines level of POSIX compliance])
	    AC_DEFINE([_XOPEN_SOURCE_EXTENDED], [1],
		[Obtains extended POSIX API])
	    AC_DEFINE([HPUX_SOURCE], [1],
		[Define to obtain optional system API])
	    ;;
	FreeBSD)
	    ;;
	IRIX*)
	    AC_DEFINE([_XOPEN_SOURCE], [500],
		[Defines level of POSIX compliance])
	    AC_DEFINE([SGI_SOURCE], [1],
		[Define to obtain optional system API])
	    AC_DEFINE([BSD_TYPES], [1],
		[Define to obtain optional system API])
	    ;;
	Linux)
	    AC_DEFINE([_XOPEN_SOURCE], [500],
		[Defines level of POSIX compliance])
	    AC_DEFINE([BSD_SOURCE], [1],
		[Define to obtain optional system API])
	    ;;
	OSF1)
	    AC_DEFINE([_XOPEN_SOURCE], [500],
		[Defines level of POSIX compliance])
	    AC_DEFINE([OSF_SOURCE], [1],
		[Define to obtain optional system API])
	    ;;
	SunOS)
	    AC_DEFINE([_XOPEN_SOURCE], [500],
		[Defines level of POSIX compliance])
	    AC_DEFINE([_EXTENSIONS__], [1],
		[Obtains optional system API])
	    #
            # The following is a hack to prevent SunOS 5.10's c89(1) from
            # defining c99(1) features and, consequently, failing to compile the
            # LDM package.
	    #
	    test `uname -r` = 5.10 && \
		CPPFLAGS="${CPPFLAGS+$CPPFLAGS }-U__C99FEATURES__"
	    ;;
    esac
    case "${CPPFLAGS}" in
	*NDEBUG*);;
	*) CPPFLAGS="${CPPFLAGS+$CPPFLAGS }-DNDEBUG";;
    esac
    AC_SUBST([CPPFLAGS])
])


dnl Check for mmap(2).
dnl
AC_DEFUN([UD_MMAP], [dnl
    AC_MSG_CHECKING(mmap());
    case `uname -s` in
    ULTRIX)
	AC_MSG_RESULT(no)
	;;
    IRIX*)
	AC_DEFINE(HAVE_MMAP, 1, [Creates pq(3) module that uses mmap(2)])
	AC_MSG_RESULT(yes)
	;;
    OSF1)
	AC_DEFINE(HAVE_MMAP, 1, [Creates pq(3) module that uses mmap(2)])
	AC_MSG_RESULT(yes)
	;;
    *)
	AC_DEFINE(HAVE_MMAP, 1, [Creates pq(3) module that uses mmap(2)])
	AC_MSG_RESULT(yes)
	;;
    esac
])


dnl Find a POSIX shell.
dnl
AC_DEFUN([UD_PROG_SH], [dnl
    AC_ARG_VAR([SH], [POSIX shell command])
    AC_MSG_CHECKING([for POSIX shell])
    case "$SH" in
    '')
	SH=/bin/sh
	case `uname -sv` in
	'AIX 4')
	    SH=/bin/bsh
	    ;;
	ULTRIX*)
	    SH=/usr/local/gnu/bin/bash
	    ;;
	esac
	;;
    esac
    AC_MSG_RESULT($SH)
    AC_SUBST([SH])
])dnl


dnl Find the perl(1) program.
dnl
AC_DEFUN([UD_PROG_PERL],
[dnl
    AC_PROG_EGREP
    AC_ARG_VAR([PERL], [version 5 perl(1) command])
    AC_CACHE_CHECK(
	[for version 5 perl(1) utility],
	[ac_cv_path_PERL],
	[AC_PATH_PROGS_FEATURE_CHECK([PERL], [perl],
	    [dnl
		if $ac_path_PERL -v | $EGREP 'version 5|v5\.' >/dev/null; then
		    ac_cv_path_PERL=$ac_path_PERL
		    ac_path_PERL_found=:
		fi
	    ],
	    [AC_MSG_ERROR([Could not find version 5 perl(1) utility])])])
    AC_SUBST([PERL], [$ac_cv_path_PERL])
])dnl


dnl Find the rpcgen(1) program.
dnl
AC_DEFUN([UD_PROG_RPCGEN], [dnl
    AC_ARG_VAR([RPCGEN], [rpcgen(1) command])
    AC_CACHE_VAL([ac_cv_path_RPCGEN], [AC_PATH_PROG([RPCGEN], [rpcgen])])
    if test -z "$RPCGEN"; then
	AC_MSG_NOTICE([Could not find rpcgen(1) utility. Setting to "rpcgen".])
        AC_SUBST([RPCGEN], [rpcgen])
    fi
])dnl


dnl Find the ntpdate(8) program.
dnl
AC_DEFUN([UD_PROG_NTPDATE], [dnl
    AC_ARG_VAR([NTPDATE], [time utility (e.g., ntpdate, chronyd)])
    AC_CACHE_VAL([ac_cv_path_NTPDATE],
	[AC_PATH_PROGS([NTPDATE], [chronyd ntpdate], [ntpdate],
	    [$PATH$PATH_SEPARATOR/usr/sbin$PATH_SEPARATOR/sbin])])
])dnl


dnl Find the netstat(1) command.
dnl
AC_DEFUN([UD_PROG_NETSTAT], [dnl
    AC_ARG_VAR([NETSTAT], [netstat(1) command])
    AC_CACHE_CHECK(
	[for netstat(1) command],
	[ac_cv_path_NETSTAT],
	[AC_PATH_PROGS_FEATURE_CHECK([NETSTAT], [netstat],
	    [dnl
		for options in '-f inet -P tcp -n' '-f inet -p tcp -n' \
			'-A inet -t -n'; do
		    cmd="$ac_path_NETSTAT $options"
                    dnl The test is for no standard error output because
                    dnl some netstat(1)s have an exit code of 0 even if
                    dnl invoked incorrectly (Fedora's, for example)
		    if test -z "`$cmd 2>&1 >/dev/null`"; then
			ac_cv_path_NETSTAT=$cmd
			ac_path_NETSTAT_found=:
			break
		    fi
		done
	    ],
	    [
		AC_MSG_WARN("Could not find netstat(1).  Using true(1)")
		ac_cv_path_NETSTAT=true
	    ])])
    AC_SUBST([NETSTAT], [$ac_cv_path_NETSTAT])
])dnl


dnl Find the top(1) command.
dnl
AC_DEFUN([UD_PROG_TOP],
[dnl
    AC_ARG_VAR([TOP], [top(1) command])
    AC_CACHE_CHECK(
	[for top(1) command],
	[ac_cv_path_TOP],
	[AC_PATH_PROGS_FEATURE_CHECK([TOP], [top],
	    [dnl
		for options in '-b -n 1' '-n 0 -l 1'; do
		    cmd="$ac_path_TOP $options"
		    if $cmd >/dev/null 2>&1; then
			ac_cv_path_TOP=$cmd
			ac_path_TOP_found=:
			break
		    fi
		done
	    ],
	    [
		AC_MSG_WARN("Could not find top(1).  Using true(1)")
		ac_cv_path_TOP=true
	    ])])
    AC_SUBST([TOP], [$ac_cv_path_TOP])
])dnl


dnl Set the fully-qualified domain name.
dnl
AC_DEFUN([UD_FQDN], [dnl
AC_MSG_CHECKING(fully-qualified domain name)
if test -z "$FQDN"; then
    name=`uname -n || hostname`
    FQDN=
    case "$name" in
    *.*)FQDN=$name
	;;
    *)	domain=`domainname`
	case "$domain" in
	noname|"")
	    entry=`grep $name /etc/hosts | head -1`
	    if test -n "$entry"; then
		FQDN=`echo "$entry" | \
		    sed "s/.*\($name\.[[a-zA-Z0-9_.]]*\).*/\1/"`
	    fi
	    ;;
	*)  FQDN=${name}.${domain}
	    ;;
	esac
	;;
    esac
    case "$FQDN" in
    "") echo 1>&2 "Couldn't obtain fully-qualified domain-name"
	echo 1>&2 "Set environment variable FQDN and rerun configure"
	echo 1>&2 "    E.g. setenv FQDN foo.unidata.ucar.edu"
	exit 1
	;;
    esac
    AC_MSG_RESULT($FQDN)
    AC_SUBST(FQDN)dnl
fi
])


dnl Set the domain name.
dnl
AC_DEFUN([UD_DOMAINNAME], [dnl
AC_MSG_CHECKING(domain name)
if test -z "$DOMAINNAME"; then
    name=`uname -n || hostname`
    case "$name" in
    *.*)
	changequote(,)dnl
	DOMAINNAME=`echo $name | sed 's/[^.]*\.//'`
	changequote([,])dnl
	;;
    *)	domain=`domainname 2>/dev/null`
	case "$domain" in
	noname|"")
	    entry=`grep $name /etc/hosts | head -1`
	    if test -n "$entry"; then
		DOMAINNAME=`echo "$entry" | \
		    sed "s/.*$name\.\([[a-zA-Z0-9_.]]*\).*/\1/"`
	    fi
	    ;;
	*)  DOMAINNAME=$domain
	    ;;
	esac
	;;
    esac
    case "$DOMAINNAME" in
    "") echo 1>&2 "Couldn't obtain domain-name"
	echo 1>&2 "Set environment variable DOMAINNAME and rerun configure"
	echo 1>&2 "    E.g. setenv DOMAINNAME unidata.ucar.edu"
	exit 1
	;;
    esac
    AC_MSG_RESULT($DOMAINNAME)
    AC_SUBST(DOMAINNAME)dnl
fi
])


dnl Set RPC and socket references.
dnl
AC_DEFUN([UD_NETWORKING],
[dnl
    AC_MSG_CHECKING(networking references)
    case `uname -sr` in
	"SunOS 5"*)
	    case "$LIBS" in
		*-lnsl*)
		    libs=
		    ;;
		*)  libs="-lnsl"
		    ;;
	    esac
	    case "$LIBS" in
		*-lsocket*)
		    ;;
		*)
		    libs="${libs:+$libs }-lsocket"
		    ;;
	    esac
	    ;;
	"SunOS 4"*)
	    libs=
	    ;;
	BSD*)
	    libs="-lrpc"
	    ;;
	HP-UX\ ?.10.2*)
	    libs="-lnsl_s -lPW"
	    ;;
	HP-UX\ ?.11*)
	    ;;
	*)  libs=
	    ;;
    esac
    AC_MSG_RESULT($libs)
    LIBS="${LIBS:+$LIBS }$libs"
    unset libs
])


dnl dnl Set Berkeley socket references.
dnl dnl
dnl AC_DEFUN([UD_SOCKET],
dnl [dnl
dnl     AC_MSG_CHECKING(Berkeley socket references)
dnl     case `uname -s` in
dnl 	SunOS)
dnl 	    case `uname -r` in
dnl 		5*)	case "$LIBS" in
dnl 			*-lsocket*)
dnl 			    AC_MSG_RESULT()
dnl 			    ;;
dnl 			*)  LIBS="$LIBS -lsocket"
dnl 			    AC_MSG_RESULT(-lsocket)
dnl 			    ;;
dnl 		    esac
dnl 		    ;;
dnl 		*)  AC_MSG_RESULT()
dnl 		    ;;
dnl 	    esac
dnl 	    ;;
dnl 	*)  AC_MSG_RESULT()
dnl 	    ;;
dnl     esac
dnl ])


dnl dnl Set RPC references.
dnl dnl
dnl AC_DEFUN([UD_RPC],
dnl [dnl
dnl     AC_MSG_CHECKING(RPC references)
dnl     case `uname -s` in
dnl 	SunOS)
dnl 	    case `uname -r` in
dnl 		5*)
dnl 		    case "$LIBS" in
dnl 			*-lrpcsoc*)
dnl 			    AC_MSG_RESULT()
dnl 			    ;;
dnl 			*)  LIBS="$LIBS -R/usr/ucblib -L/usr/ucblib -lrpcsoc -lnsl"
dnl 			    AC_MSG_RESULT(-R/usr/ucblib -L/usr/ucblib -lrpcsoc -lnsl)
dnl 			    ;;
dnl 		    esac
dnl 		    AC_DEFINE(PORTMAP)
dnl 		    ;;
dnl 		*)  AC_MSG_RESULT()
dnl 		    ;;
dnl 	    esac
dnl 	    ;;
dnl 	BSD*)
dnl 	    LIBS="$LIBS -lrpc"
dnl 	    AC_MSG_RESULT(-lrpc)
dnl 	    ;;
dnl 	*)  AC_MSG_RESULT()
dnl 	    ;;
dnl     esac
dnl ])


dnl Set ulog parameters
dnl
AC_DEFUN([UD_ULOG], [dnl
    AC_MSG_CHECKING([for system logging daemon's configuration-file])
    SYSLOG_CONF=`ls -t /etc/syslog-ng/syslog-ng.conf /etc/rsyslog.conf \
        /etc/opt/csw/rsyslog.conf /etc/syslog.conf 2>/dev/null | head -1`
    AC_MSG_RESULT($SYSLOG_CONF)
    AC_SUBST([SYSLOG_CONF])

    if test -e $LDMHOME/logs; then
        AC_SUBST([LDM_LOGFILE], [$LDMHOME/logs/ldmd.log])
    else
        AC_SUBST([LDM_LOGFILE], [$LDMHOME/var/logs/ldmd.log])
    fi

    case `uname -sr` in
	OSF1*|sn1036*|Linux*|Darwin*)
            AC_MSG_CHECKING([if ulog(3) should define syslog(3)])
	    AC_DEFINE(NO_REPLACE_SYSLOG, 1,
		[Causes syslog(3) to not be defined in the ulog(3) module])
	    AC_MSG_RESULT([no])
	    ;;
	*)
            AC_MSG_CHECKING([whether syslog(3) returns an int])
	    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <syslog.h>]], [[int i = syslog(0,0);]])],[AC_DEFINE(SYSLOG_RETURNS_INT, 1,
		    Whether syslog(3) returns an int)
		AC_MSG_RESULT(yes)],[AC_MSG_RESULT([no])])
	    ;;
    esac

    AC_MSG_CHECKING([pathname of system log file])
    unset ULOGNAME
    for usock in /dev/log /var/run/syslog; do
	if test -w $usock; then
	    ULOGNAME=$usock
	    AC_DEFINE_UNQUOTED(ULOGNAME, "$ULOGNAME", 
		[Pathname of the system log file])
	    AC_MSG_RESULT([$ULOGNAME])
            AC_MSG_CHECKING([if system log file is a socket])
	    if ls -lL $usock | grep '^s' >/dev/null; then
		AC_DEFINE(LOGNAME_ISSOCK, 1,
		    [Define if the system log file is a socket])
		AC_MSG_RESULT([yes])
            else
		AC_MSG_RESULT([no])
	    fi
	    break
	fi
    done

    if test -z "$ULOGNAME"; then
        AC_MSG_RESULT([not found])
        AC_MSG_CHECKING([pathname of console])
	if test -w /dev/conslog; then
	    AC_DEFINE(_DEV_CONSLOG, 1,
		[Define if the file /dev/conslog exists])
	    AC_MSG_RESULT([/dev/conslog])
	else
	    AC_MSG_RESULT([no console found])
	fi
    fi
])


dnl Set syslog pid filename for hupsyslog
dnl
AC_DEFUN([UD_SYSLOG_PIDFILE], [dnl
    AC_MSG_CHECKING([for system logging daemon's PID file])
    path=`ls -t /var/run/rsyslogd.pid /var/run/syslog.pid \
        /var/run/syslogd.pid /etc/syslog.pid 2>/dev/null | head -1`
    AC_MSG_RESULT($path)
    AC_DEFINE_UNQUOTED(SYSLOG_PIDFILE, "$path",
		[Pathname of system logging daemon's PID file])
])dnl


dnl  Run configure in another directory
dnl  Useful when merging packages.
dnl
AC_DEFUN([UD_SUBCONFIG],
[
  if test -d $1; then
    if test -f $1/config.status; then
	    echo " * running config.status in $1"
	    (cd $1 ; ${CONFIG_SHELL-/bin/sh} ./config.status)
	else
	    echo "running configure in $1"
	    (cd $1 ; ${CONFIG_SHELL-/bin/sh} ./configure --prefix=${prefix})
    fi
  else
	echo "$1 not found"
  fi
])dnl


dnl  Set up LDMHOME
dnl
AC_DEFUN([UD_LDMHOME],
[
	AC_MSG_CHECKING(LDMHOME)
	case "$LDMHOME" in
	    '')	LDMHOME=`awk -F: '{if($''1~/ldm/) {print $''6;exit;}}' /etc/passwd`
		case "$LDMHOME" in
		    '') AC_MSG_ERROR(\$LDMHOME not set.  Set and re-execute configure script.)
			;;
		esac
		;;
	esac
	AC_SUBST(LDMHOME)
	AC_DEFINE_UNQUOTED(LDMHOME, "$LDMHOME", [Top-level install-point])
	AC_MSG_RESULT($LDMHOME)
])dnl


dnl  Set up SRCDIR
dnl
AC_DEFUN([UD_SRCDIR],
[
	AC_MSG_CHECKING(LDM source directory)
	SRCDIR=${SRCDIR-`pwd`}
	AC_SUBST(SRCDIR)
	AC_MSG_RESULT($SRCDIR)
])dnl


AC_DEFUN([UD_PROG_CPP],
[dnl
    AC_REQUIRE([AC_PROG_CC]) dnl
    case `uname -sr` in
	'HP-UX B.11'*)
	    cpp="${CC-c89} -E"	dnl "-w" disables warnings
	    ;;
	*)
	    AC_REQUIRE([AC_PROG_CPP]) dnl
	    AC_MSG_CHECKING(the C preprocessor)
	    AC_PREPROC_IFELSE([AC_LANG_SOURCE([[#include <stdlib.h>]])],[AC_MSG_RESULT(works)],[AC_MSG_ERROR([$[]0: C preprocessor, $CPP, doesn't work])])
	    ;;
    esac
])


AC_DEFUN([UD_HPUX], [
    AC_MSG_CHECKING([for HP-UX])
    AC_BEFORE([$0], [AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[],[])])
    AC_BEFORE([$0], [AC_RUN_IFELSE([AC_LANG_SOURCE([[]])],[],[],[])])
    AC_BEFORE([$0], [AC_EGREP_HEADER])
    AC_EGREP_CPP([yes],[
	    #ifdef __hpux
	      yes
	    #endif
	],[
	    AC_MSG_RESULT(yes)
dnl	Under HP-UX B.11.00, <rpc/rpc.h> includes <rpc/auth.h>, which includes
dnl	<sys/user.h>, which uses "kt_t", which isn't defined anywhere; so omit
dnl	<sys/user.h>
	    AC_DEFINE(_SYS_USER_INCLUDED, 1,
		[Causes <sys/user.h> to not be included])
	    case "$CPP" in
	    /bin/c89*|c89*|/bin/cc*|cc*|/lib/cpp*|cpp*)
dnl 	HP-UX's C compiler emits a warning about <stdlib.h> when
dnl	HPUX_SOURCE is defined.  This fouls tests that check output
dnl	on standard error.  Consequently, ensure that warning messages
dnl	are suppressed.
		case "$CFLAGS" in
		*-w*);;
		esac;;
	    esac
	    case "$CFLAGS" in
dnl	In 64-bit mode, the "xnet" networking library must be used.
	    *+DA2.0W*) LIBS="${LIBS}${LIBS+ }-lxnet";;
	    esac
	],[AC_MSG_RESULT(no)
    ])dnl
])dnl


AC_DEFUN([UD_SIG_ATOMIC_T], [
    AC_MSG_CHECKING([for sig_atomic_t in signal.h])
    AC_EGREP_HEADER(sig_atomic_t, signal.h,
		    AC_MSG_RESULT(defined),
		    [
			AC_MSG_RESULT(not defined)
			AC_DEFINE(sig_atomic_t, int,
			    [Type of atomic signal-handler variable])
		    ]
		   )
])

dnl
dnl Change defaults to be compatible with Peter Neilley's "weather" program
dnl
AC_DEFUN([UD_NEILLEY_COMPAT], [dnl
   AC_SUBST(GDBMLIB)
   if test -z "$GDBMLIB"; then
       AC_CHECK_LIB(gdbm, gdbm_open, GDBMLIB=-lgdbm)
   fi
   if test -n "$GDBMLIB"; then
       AC_CHECK_HEADER(gdbm.h, AC_DEFINE(USE_GDBM, 1,
	[Use the gdbm(3) library instead of the ndbm(3) library]))
   fi
   AC_DEFINE([DB_XPROD], 0,
       [Define to DBFILE entire data-product and not just data])
   AC_DEFINE([DB_CONCAT], 1,
       [Define to concatenate DBFILE products with same key])
])dnl


dnl Turn off DB support if not available
dnl
AC_DEFUN([UD_DB], [dnl
   if test -z "$GDBMLIB"; then
       AC_MSG_NOTICE("GDBMLIB not set")
       AC_CHECK_FUNC(dbm_open,
          ,
          AC_DEFINE(NO_DB, 1, [Disables the DBFILE capability])
          AC_MSG_WARN("pqact DBFILE action disabled")
       )
   fi
])dnl


dnl Set the value of a variable.  Use the environment if possible; otherwise
dnl set it to a default value.  Call the substitute routine.
dnl
AC_DEFUN([UD_DEFAULT], [dnl
    $1=${$1-"$2"}
    AC_SUBST([$1])
])


dnl Form a library reference for the linker/loader
dnl
dnl On a SunOS 5 system, a `-R<dir>' is added in addition to a `-L<dir>'
dnl in order to make the utility independent of LD_LIBRARY_PATH (is this
dnl a good idea?) and to ensure that it'll run regardless of who
dnl executes it.
dnl
dnl UC_LINK_REF(varname, libdir, libname)
dnl
dnl Example: UC_LINK_REF(UC_LD_MATH, /upc/netcdf/lib, netcdf)
dnl
AC_DEFUN([UD_LINK_REF], [dnl
    case `uname -rs` in
	unicos*)
	    case "$2" in
		'') $1="-l $3";;
		*)  $1="-L $2 -l $3";;
	    esac
	    ;;
	SunOS\ 5*)
	    case "$2" in
		'') $1="-l$3";;
		*)  $1="-R$2 -L$2 -l$3";;
	    esac
	    ;;
	*)
	    case "$2" in
		'') $1="-l$3";;
		*)  $1="-L$2 -l$3";;
	    esac
	    ;;
    esac
])

dnl @synopsis TYPE_SOCKLEN_T
dnl
dnl Check whether sys/socket.h defines type socklen_t. Please note
dnl that some systems require sys/types.h to be included before
dnl sys/socket.h can be compiled.
dnl
dnl @version $Id: aclocal.m4,v 1.27.2.1.2.1.2.1.2.23 2009/08/21 19:58:25 steve Exp $
dnl @author Lars Brinkhoff <lars@nocrew.org>
dnl
AC_DEFUN([TYPE_SOCKLEN_T],
[AC_CACHE_CHECK([for socklen_t], ac_cv_type_socklen_t,
[
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
   #include <sys/socket.h>]], [[socklen_t len = 42; return 0;]])],[ac_cv_type_socklen_t=yes],[ac_cv_type_socklen_t=no])
])
  if test $ac_cv_type_socklen_t != yes; then
    AC_DEFINE(socklen_t, int, [Type of variable for holding socket-length data])
  fi
])


dnl Setup for making a manual-page database.
dnl
AC_DEFUN([UD_MAKEWHATIS],
[
    #
    # NB: We always want to define WHATIS to prevent the
    # $(mandir)/$(WHATIS) make(1) target from being just $(mandir)/ and
    # conflicting with the (directory creation) target with the same name.
    #
    WHATIS=whatis
    case "`uname -sr`" in
	(BSD/OS*|FreeBSD*|Darwin*)
	    # Can't generate a user-database -- only /usr/share/man/whatis.db.
	    MAKEWHATIS_CMD=
	    ;;
	('IRIX64 6.5'|'IRIX 6.5')
	    MAKEWHATIS_CMD='/usr/lib/makewhatis -M $(mandir) $(mandir)/whatis'
	    ;;
	('IRIX 6'*)
	    # Can't generate a user-database.
	    MAKEWHATIS_CMD=
	    ;;
	(HP-UX*)
	    # Can't generate a user-database -- only /usr/lib/whatis.
	    MAKEWHATIS_CMD=
	    ;;
	('Linux '*)
	    # /usr/sbin/makewhatis doesn't work
	    MAKEWHATIS_CMD=
	    ;;
	(ULTRIX*)
	    # Can't generate a user-database -- only /usr/lib/whatis.
	    MAKEWHATIS_CMD=
	    ;;
	(*)
	    if test -r /usr/man/windex; then
		WHATIS=windex
	    fi
	    AC_CHECK_PROGS(prog, catman makewhatis /usr/lib/makewhatis, [catman])
	    case "$prog" in
		(*catman*)
		    MAKEWHATIS_CMD=$prog' -w -M $(mandir)'
		    ;;
		(*makewhatis*)
		    MAKEWHATIS_CMD=$prog' $(mandir)'
		    ;;
	    esac
	    ;;
    esac
    AC_SUBST(WHATIS)
    AC_SUBST(MAKEWHATIS_CMD)
    AC_MSG_CHECKING(for manual-page index command)
    AC_MSG_RESULT($MAKEWHATIS_CMD)
])

dnl Set an output variable to a C-preprocessor reference for a C header-file
dnl if necessary.
dnl     $1  Name component (e.g., "XML2")
dnl     $2  Description
dnl     $3  Space-separated list of directories
dnl     $4  Name of the header-file (may contain "/")
AC_DEFUN([UD_CHECK_HEADER],
[
    AC_ARG_VAR([CPPFLAGS_$1], [$2])
    AC_CACHE_CHECK([for header-file <$4>], [ac_cv_header_$1],
        [for dir in $3; do
            if test -r $dir/$4; then
                ac_cv_header_$1="-I $dir"
                break
            fi
        done])
    if test -z "$ac_cv_header_$1"; then
        AC_MSG_ERROR([Header-file <$4> not found])
    else
        CPPFLAGS_$1="$ac_cv_header_$1"
    fi
])


dnl Sets a linker reference to a library if necessary.
dnl     $1  Name component (e.g., "XML2")
dnl     $2  Name of a function
dnl     $3  Space-separated list of directories
dnl     $4  Name of output variable
AC_DEFUN([UD_SEARCH_LIB],
[
    AC_MSG_NOTICE([Searching for the "$1" library])
    AC_SEARCH_LIBS([$2], [$1], ,
        [origLibs="$LIBS"
        found=no
        for dir in "" $3; do
            LIBS="${dir:+-L$dir }-l$1${LIBS:+ $LIBS}"
            #AC_MSG_NOTICE([LIBS = "$LIBS"])
            AC_CHECK_FUNC([$2], [found=yes; break])
            LIBS="$origLibs"
            unset ac_cv_func_$2
        done
        if test $found != yes; then
            AC_MSG_ERROR(["$1" library not found])
        else
            AC_MSG_NOTICE(["$1" library found in "$dir"])
            LIBS="$origLibs"
            test "$LIBS" || unset LIBS
            $4="${dir:+-L$dir }-l$1"
            AC_SUBST($4)
        fi
        unset found]
    )
])

divert(diversion_number)dnl
