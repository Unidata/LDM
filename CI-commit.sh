# This script does the following:
#     1. Builds the package;
#     2. Tests the package; and
#     3. Creates a source-distribution.

set -e  # exit if error

#
# Build and test the package and create a source-distribution.
#
mkdir -p m4
autoreconf -i
./configure --disable-root-actions &>configure.log
make distcheck DISTCHECK_CONFIGURE_FLAGS=''
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-multicast'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-gribinsert'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-noaaport'
make distcheck DISTCHECK_CONFIGURE_FLAGS='--with-noaaport --with-retrans'
