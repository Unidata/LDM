# Performs an acceptance-test of a package on a Linux system.
#
# Usage: $0 tarball vmName
#
# where:
#       tarball         Pathname of source tarball.
#       vmName          Name of the Vagrant virtual machine (e.g.,
#                       "centos64_64", "precise32")

set -e

tarball=${1:?Pathname of tarball not specified}
vmName=${2:?Name of Vagrant virtual-machine not specified}

# Copy the tarball to the current working directory.
#
cp $tarball .

# Start the virtual machine. Ensure that each virtual machine is started
# separately because vagrant(1) doesn't support concurrent "vagrant up" 
# invocations.
#
trap "vagrant destroy --force $vmName; `trap -p EXIT`" EXIT
flock "$SOURCE_DISTRO_NAME" -c "vagrant up \"$vmName\""

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
