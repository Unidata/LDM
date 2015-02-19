# This script is intended to be executed on the continuous-integration server
# and does the following:
#     1. Builds the package;
#     2. Tests the package; and
#     3. Creates a source-distribution.

set -e  # exit if error

# Get the static release variables
#
. ./release-vars.sh

#
# Build and test the package and create a source distribution.
#
mkdir -p m4 mcast_lib/vcmtp/m4
autoreconf -if
./configure --disable-root-actions >configure.log 2>&1
make distcheck
make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-multicast'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-gribinsert'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-noaaport'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-noaaport --with-retrans'
