# Deploys the LDM to a remote host.
#
# Usage:
#       $0 srcDistroPath host [configOpts]
#
# where:
#       srcDistroPath    Path of compressed tar(1) file containing the source
#       host             Name of the remote computer on which to deploy the LDM
#       configOpts       Optional configure(1) script options

set -e # terminate on error

srcDistroPath=${1:?Source distribution not specified}
host=${2:?Host name not specified}
configOpts=$3

srcDistroName=`basename $srcDistroPath`
pkgName=`basename $srcDistroName .tar.gz`

# Copy the source-distribution to the remote host.
#
scp $srcDistroPath ldm@$host:
trap "ssh -T ldm@$host rm -f $srcDistroName; `trap -p ERR`" ERR

# As the LDM user on the remote host, unpack, build, and install the package.
#
ssh -T ldm@$host bash --login -x -e <<EOF
gunzip -c $srcDistroName | pax -r '-s:/:/src/:'
trap "rm -rf \$HOME/$pkgName; \`trap -p ERR\`" ERR
rm $srcDistroName
cd $pkgName/src
./configure --disable-root-actions ${configOpts} CFLAGS=-g >configure.log 2>&1
make install >install.log 2>&1
EOF

# As the superuser on the remote host, perform the root actions.
#
ssh -T root@$host bash --login -x -e <<EOF
ldmHome=\`awk -F: '\$1~/^ldm$/{print \$6}' /etc/passwd\`
cd \$ldmHome/$pkgName/src
make root-actions >root-actions.log 2>&1
EOF

# As the LDM user on the remote host, execute the new package.
#
ssh -T ldm@$host bash --login -x -e <<EOF
ldmadmin isrunning && ldmadmin stop
rm -f runtime
ln -s $pkgName runtime
ldmadmin start
EOF
