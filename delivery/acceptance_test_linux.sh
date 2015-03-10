# Performs an acceptance-test of a package on a Linux virtual-machine system.
#
# Preconditions:
#       The current working directory contains the following:
#           - The source-distribution tarball;
#           - The release-variables file "release-vars.sh"; and
#           - The Vagrant-file "Vagrantfile".

# Usage: $0 vmName
#
# where:
#       vmName          Name of the Vagrant virtual machine (e.g.,
#                       "centos64_64", "precise32")

set -e  # terminate on error

# Parse the command-line
#
vmName=${1:?Name of Vagrant virtual-machine not specified}

# Set the release-variables.
#
. ./release-vars.sh

# Start the virtual machine.
#
# Problems with concurrent Vagrant/VirtualBox VM-s exist with Vagrant 1.6 and
# 1.7.2, and regardless of whether the "vagrant up" command is made sequential
# by a one-line flock(1) command or by an flock(1) block. Consequently,
# the concurrent Vagrant/VirtualBox VM-s are serialized.
#
#( flock 9; vagrant up $vmName;
#) 9>/tmp/`basename $0`-$USER
flock . vagrant up $vmName
trap "vagrant destroy --force $vmName; `trap -p EXIT`" EXIT

# On the virtual machine:
#
vagrant ssh $vmName -- -T <<EOF
    set -e

    # Unpack the source distribution.
    #
    pax -zr -s:/:/src/: </vagrant/*.tar.gz

    # Make the source directory of the unpacked distribution the current working
    # directory because that's where the "configure" script is.
    #
    cd $RELPATH_DISTRO_SOURCE_DIR

    # Build the package from source, test it, and install it.
    #
    ./configure $ACCEPTANCE_CONFIGURE_OPTS
    make all check install
EOF
#) 9>/tmp/`basename $0`-$USER