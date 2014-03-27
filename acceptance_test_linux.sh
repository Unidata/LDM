# Performs an acceptance-test of a package on a Linux system. Assumes that
# this script is in the top-level of the development source-directory and that
# the necessary files already exist.
#
# Usage: $0 vmName
#
# where:
#       vmName          Name of the Vagrant virtual machine (e.g.,
#                       "centos64_64", "precise32")

set -e

vmName=${1:?Name of Vagrant virtual-machine not specified}

# Make the directory that contains this script be the current working directory.
#
cd `dirname $0`

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
