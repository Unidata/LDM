# Deploys the LDM to a remote host. Assumes that this script is in the top-level
# development source-directory and that the necessary files already exist.
#
# Usage:
#       $0 host [configOpts]
#
# where:
#       host             Name of the remote computer on which to deploy the LDM
#       configOpts       Optional configure(1) script options

set -e # terminate on error

host=${1:?Host name not specified}
configOpts=$2

# For convenience, make the directory that contains this script be the current
# working directory.
cd `dirname $0`

srcDistroName=`ls *.tar.gz`
pkgName=`basename $srcDistroName .tar.gz`

# Copy the source-distribution to the remote host.
#
scp $srcDistroName ldm@$host:
trap "ssh -T ldm@$host rm -f $srcDistroName; `trap -p ERR`" ERR

# bash(1) is explicitly used for remote executions because 1) not all LDM users
# have the same user-shell; and 2) not all sh(1)-s behave the same -- especially
# in the handling of substitutions, quotes, and escapes.

# As the LDM user on the remote host, unpack, build, and install the package.
#
ssh -T ldm@$host bash --login <<EOF
    set -x -e
    gunzip -c $srcDistroName | pax -r '-s:/:/src/:'
    trap "rm -rf \$HOME/$pkgName; \`trap -p ERR\`" ERR
    rm $srcDistroName
    cd $pkgName/src
    ./configure --disable-root-actions ${configOpts} CFLAGS=-g >configure.log 2>&1
    make install >install.log 2>&1
EOF

# As the superuser on the remote host, perform the root actions.
#
ssh -T root@$host bash --login <<EOF
    set -x -e
    ldmHome=\`awk -F: '\$1~/^ldm$/{print \$6}' /etc/passwd\`
    cd \$ldmHome/$pkgName/src
    make root-actions >root-actions.log 2>&1
EOF

# As the LDM user on the remote host, execute the new package.
#
ssh -T ldm@$host bash --login <<EOF
    set -x -e
    ldmadmin isrunning && ldmadmin stop
    rm -f runtime
    ln -s $pkgName runtime
    ldmadmin start
EOF
