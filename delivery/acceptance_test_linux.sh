# Performs an acceptance-test of a package on a Linux system. Builds the package
# in the directory that contains this script and assumes that the tarball is one
# directory level up.
#
# Usage: $0 vmName
#
# where:
#       vmName          Name of the Vagrant virtual machine (e.g.,
#                       "centos64_64", "precise32")

set -e  # terminate on error

vmName=${1:?Name of Vagrant virtual-machine not specified}

# Go to the build directory.
#
cd `dirname $0`

# Move the tarball to the build directory so that it appears in the "/vagrant"
# directory.
#
mv ../*.tar.gz .

# Set the release variables.
#
. ./release-vars.sh

# Start the virtual machine.
#
trap "vagrant destroy --force $vmName; `trap -p EXIT`" EXIT
vagrant up "$vmName"

# On the virtual machine,
#
vagrant ssh $vmName -- -T <<EOF
    set -e

    # Unpack the source distribution.
    #
    pax -zr -s:/:/src/: </vagrant/$SOURCE_DISTRO_NAME

    # Make the source directory of the unpacked distribution the current working
    # directory because that's where the "configure" script is.
    #
    cd $RELPATH_DISTRO_SOURCE_DIR

    # Build the package from source, test it, and install it.
    #
    ./configure $ACCEPTANCE_CONFIGURE_OPTS
    make all check install
EOF
