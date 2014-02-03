# This script does the following:
#     1. Builds the package;
#     2. Tests the package; and
#     3. Creates a source-distribution.

set -e  # exit if error

#
# Build and test the package and create a source-distribution.
#
mkdir -p m4
autoreconf -i --force
./configure --disable-root-actions --with-noaaport --with-retrans \
        --with-gribinsert --with-multicast &>configure.log
make distcheck
#make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions'
#make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-multicast'
#make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-gribinsert'
#make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-noaaport'
#make distcheck DISTCHECK_CONFIGURE_FLAGS='--disable-root-actions --with-noaaport --with-retrans'
