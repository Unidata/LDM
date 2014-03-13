# Performs an acceptance-test of a package on a Linux system. Creates a binary
# distribution file and a documentation distribution file.
#
# The following environment variables must be set:
#     SOURCE_DISTRO     Glob pattern of the compressed tar file of the source
#                       distribution
#     VM_NAME           Name of the Vagrant virtual machine (e.g.,
#                       "centos64_64", "precise32")

set -e

: SOURCE_DISTRO=${SOURCE_DISTRO:?Path of source-distribution not specified}
: VM_NAME=${VM_NAME:?Name of virtual machine not specified}

prefix=/usr/local/ldm
SOURCE_DISTRO=`ls $SOURCE_DISTRO`

#
# Remove any leftover artifacts from an earlier job.
#
rm -rf *

#
# Unpack the source distribution.
#
pax -zr -s:/:/src/: <$SOURCE_DISTRO

#
# Make the source directory the current working directory.
#
cd `basename $SOURCE_DISTRO .tar.gz`/src

#
# Start the virtual machine. Ensure that each virtual machine is started
# separately because vagrant(1) doesn't support concurrent "vagrant up" 
# invocations.
#
type vagrant 
trap "vagrant destroy --force $VM_NAME; `trap -p EXIT`" EXIT
flock "$SOURCE_DISTRO" -c "vagrant up \"$VM_NAME\""

#
# On the virtual machine, build the package from source, test it, and install
# it.
#
vagrant ssh $VM_NAME -c "make --version"
vagrant ssh $VM_NAME -c \
    "./configure --prefix=$prefix --with-noaaport --disable-root-actions"
vagrant ssh $VM_NAME -c "make all check install"

#
# Create a distribution of the documentation in case it's needed by a
# subsequent job. NB: The top-level directory
# is "share/".
#
pkgId=`basename $SOURCE_DISTRO .tar.gz | sed 's/^\([^-]*-[0-9.]*\).*/\1/'`
vagrant ssh $VM_NAME -c \
    "pax -zw -s ';$prefix/share/doc/ldm;$pkgId;' $prefix/share/doc/ldm >$pkgId-doc.tar.gz"
vagrant ssh $VM_NAME -c "cp $pkgId-doc.tar.gz /vagrant"
