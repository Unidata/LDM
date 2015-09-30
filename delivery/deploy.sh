# Deploys the LDM to a remote host. Assumes that the release scripts are in the
# directory that contains this script and that the tarball is one directory
# level up.
#
# Usage:
#       $0 host [configOpts]
#
# where:
#       host             Name of the remote computer on which to deploy the LDM
#       configOpts       Optional configure(1) script options

set -e # terminate on error

host=${1:?Host name not specified}
shift

# Go to the directory that contains this script.
#
cd `dirname $0`

# Get the release variables.
#
. ./release-vars.sh

# Copy the source-distribution to the remote host.
#
scp ../$SOURCE_DISTRO_NAME $USER_NAME@$host:

# bash(1) is explicitly used for remote executions because 1) not all LDM users
# have the same user-shell; and 2) not all sh(1)-s behave the same -- especially
# in the handling of substitutions, quotes, and escapes.

# As the LDM user on the remote host, unpack, build, and install the package.
#
ssh -T $USER_NAME@$host bash --login <<EOF
    set -x -e
    gunzip -c $SOURCE_DISTRO_NAME | (mkdir -p $PKG_ID &&
        cd $PKG_ID && tar -xf - && rm -rf src && mv -f $PKG_ID src)
    cd $RELPATH_DISTRO_SOURCE_DIR
    ./configure --disable-root-actions --enable-debug $@ CFLAGS=-g
    make install
EOF

# As the superuser on the remote host, perform the root actions.
#
ssh -T root@$host bash --login <<EOF
    set -x -e
    ldmHome=\`awk -F: '\$1~/^$USER_NAME\$/{print \$6}' /etc/passwd\`
    cd \$ldmHome/$RELPATH_DISTRO_SOURCE_DIR
    make root-actions
EOF

# As the LDM user on the remote host, execute the new package.
#
ssh -T $USER_NAME@$host bash --login <<EOF
    set -x -e
    ldmadmin isrunning && ldmadmin stop
    rm -f runtime
    ln -s $PKG_ID runtime
    ldmadmin start
EOF
