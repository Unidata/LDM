# This script is intended to be executed on the continuous-integration server
# and does the following:
#     1. Builds the package;
#     2. Tests the package; and
#     3. Creates a source-distribution.

set -e  # exit if error

# Remove any artifacts left over from a previous invocation.
#
rm -f *.$SOURCE_DISTRO_EXT

#
# Build and test the package and create a source distribution.
#
mkdir -p m4 multicast/vcmtp/m4
autoreconf -iv
./configure --disable-root-actions >configure.log 2>&1
make distcheck DISTCHECK_CONFIGURE_FLAGS=''
#make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-multicast'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-gribinsert'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-noaaport'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-noaaport --with-retrans'