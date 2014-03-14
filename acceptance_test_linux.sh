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

SOURCE_DISTRO=`ls $SOURCE_DISTRO`

# Remove any leftover artifacts from an earlier job.
#
rm -rf *

# Unpack the source distribution.
#
pax -zr -s:/:/src/: <$SOURCE_DISTRO

pkgId=`basename $SOURCE_DISTRO .tar.gz`

# Make the source directory the current working directory because that's where
# "Vagrantfile" is
#
cd $pkgId/src

# Copy the source distribution to the current working directory because it will
# have to be unpacked again in the virtual machine.
#
cp $SOURCE_DISTRO .

# Start the virtual machine. Ensure that each virtual machine is started
# separately because vagrant(1) doesn't support concurrent "vagrant up" 
# invocations.
#
type vagrant 
trap "vagrant destroy --force $VM_NAME; `trap -p EXIT`" EXIT
flock "$SOURCE_DISTRO" -c "vagrant up \"$VM_NAME\""

# On the virtual machine,
#
vagrant ssh $VM_NAME -- -T <<EOF
set -e

# Unpack the source distribution.
#
pax -zr -s:/:/src/: </vagrant/`basename $SOURCE_DISTRO`

# Make the source directory the current working directory because that's where
# the "configure" script is.
#
cd $pkgId/src

# Set the installation prefix
#
prefix=\$HOME/pkgId

# Build the package from source, test it, and install it.
#
make --version
./configure --prefix=\$prefix --with-noaaport --disable-root-actions
make all check install

# Create a distribution of the documentation in case it's needed by a
# subsequent job. NB: The top-level directory will be "$pkgId" and
# "$pkgId/basics" will be one of the subdirectories.
#
pax -zw -s ";\$prefix/share/doc/ldm;$pkgId;" \$prefix/share/doc/ldm >$pkgId-doc.tar.gz
cp $pkgId-doc.tar.gz /vagrant

EOF
