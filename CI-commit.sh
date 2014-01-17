# This script does the following:
#     1. Builds the package;
#     2. Tests the package; and
#     3. Creates a source-distribution.

set -e  # exit if error

#
# Build and test the package and create a source-distribution.
#
autoreconf -i --force
./configure --disable-root-actions --with-noaaport --with-retrans \
        --with-gribinsert &>configure.log
make distcheck