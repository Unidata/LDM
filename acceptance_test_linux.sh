# Performs an acceptance-test of a package on a Linux system.
#
# Usage: $0 srcDistroPath vagrantfilePath vmName
#
# where:
#       srcDistroPath   Path of the compressed tar(1)-file source-distribution
#       vagrantfilePath Path of the relevant Vagrant configuration-file
#       vmName          Name of the Vagrant virtual machine (e.g.,
#                       "centos64_64", "precise32")

set -e

srcDistroPath=${1:?Path of source-distribution not specified}
vagrantfilePath=${2:?Path of Vagrant configuration-file not specified}
vmName=${3:?Name of Vagrant virtual-machine not specified}

# Remove any leftover artifacts from an earlier job.
#
rm -rf *

# Copy the Vagrant configuration-file and the source-distribution file to the
# current working directory so that they will be seen by Vagrant and the 
# virtual-machine, respectively.
#
cp $vagrantfilePath .
cp $srcDistroPath .

pkgId=`basename $srcDistroPath .tar.gz`

# Start the virtual machine. Ensure that each virtual machine is started
# separately because vagrant(1) doesn't support concurrent "vagrant up" 
# invocations.
#
trap "vagrant destroy --force $vmName; `trap -p EXIT`" EXIT
flock "$srcDistroPath" -c "vagrant up \"$vmName\""

# On the virtual machine,
#
vagrant ssh $vmName -- -T <<EOF
set -e

# Unpack the source distribution.
#
pax -zr -s:/:/src/: </vagrant/$pkgId.tar.gz

# Make the source directory the current working directory because that's where
# the "configure" script is.
#
cd $pkgId/src

# Build the package from source, test it, and install it.
#
./configure --with-noaaport --disable-root-actions
make all check install

EOF
