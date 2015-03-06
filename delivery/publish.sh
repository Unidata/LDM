# Copies an LDM source distribution to the public download directory and ensures
# the existence of that version's documentation on the package's website. The
# package is built and installed in "/tmp/$PKG_ID" to extract the documentation.
#
# Usage:
#       $0

set -e  # exit on failure

test $# -eq 0

# If the source repository doesn't have the source distribution,
#
if ! ssh $SOURCE_REPO_HOST test -e $ABSPATH_SOURCE_DISTRO; then
    #
    # Copy the source distribution to the source repository.
    #
    trap "ssh $SOURCE_REPO_HOST rm -f $ABSPATH_SOURCE_DISTRO; `trap -p ERR`" ERR
    scp $SOURCE_DISTRO_NAME $SOURCE_REPO_HOST:$ABSPATH_SOURCE_DISTRO
fi

# Purge the source-repository of bug-fix versions that are older than the latest
# corresponding minor release.
#
ssh -T $SOURCE_REPO_HOST bash --login <<EOF
    set -ex # Exit on error
    cd $ABSPATH_SOURCE_REPO_DIR        
    ls -d $PKG_ID_GLOB |
        sed "s/$PKG_NAME-//" |
        sort -t. -k 1nr,1 -k 2nr,2 -k 3nr,3 |
        awk -F. '\$1!=ma||\$2!=mi{print}{ma=\$1;mi=\$2}' >versions
    for vers in \`ls -d $PKG_ID_GLOB | sed "s/$PKG_NAME-//"\`; do
        fgrep -s \$vers versions || rm -rf $PKG_NAME-\$vers
    done
EOF

# Install the package so that the documentation can be copied to the website.
# "/tmp/$PKG_ID" is used as the installation point rather than anything under
# the current directory because the current directory might have spaces in its
# name and that causes problems for libtool(1).
#
rm -rf $PKG_ID
pax -zr <$SOURCE_DISTRO_NAME
trap "rm -rf $PKG_ID; `trap -p EXIT`" EXIT
cd $PKG_ID
./configure --prefix=$ABSPATH_DEFAULT_INSTALL_PREFIX --disable-root-actions \
        --with-noaaport LDMHOME=$ABSPATH_DEFAULT_LDMHOME >configure.log 2>&1
DESTDIR=/tmp/$PKG_ID
rm -rf $DESTDIR
make install DESTDIR=$DESTDIR >install.log 2>&1

# Copy the documentation to the package's website and delete the installation.
#
versionWebDirTmp=$ABSPATH_VERSION_WEB_DIR.tmp
ssh -T $WEB_HOST rm -rf $versionWebDirTmp
trap "ssh -T $WEB_HOST rm -rf $versionWebDirTmp; `trap -p ERR`" ERR
scp -Br $DESTDIR$ABSPATH_DEFAULT_INSTALL_PREFIX/$RELPATH_DOC_DIR \
        $WEB_HOST:$versionWebDirTmp
rm -r $DESTDIR

# Ensure that the package's home-page references the just-copied documentation.
ssh -T $WEB_HOST bash --login <<EOF
    set -ex  # Exit on error

    # Install the just-copied documentation.
    #
    rm -rf $ABSPATH_VERSION_WEB_DIR
    mv -f $versionWebDirTmp $ABSPATH_VERSION_WEB_DIR

    # Go to the top-level of the package's web-pages.
    #
    cd $ABSPATH_PKG_WEB_DIR

    # Allow group write access to all created files.
    #
    umask 02

    # Set the hyperlink references in the top-level HTML file. For a given major
    # and minor version, keep only the latest bug-fix.
    #
    ls -d $PKG_ID_GLOB |
        sed "s/$PKG_NAME-//" |
        sort -t. -k 1nr,1 -k 2nr,2 -k 3nr,3 |
        awk -F. '\$1!=ma||\$2!=mi{print}{ma=\$1;mi=\$2}' >versions
    sed -n '1,/$BEGIN_VERSION_LINKS/p' index.html >index.html.new
    for vers in \`cat versions\`; do
        href=$PKG_NAME-\$vers
        echo "            <li><a href=\"\$href\">\$vers</a>" >>index.html.new
    done
    sed -n '/$END_VERSION_LINKS/,\$p' index.html >>index.html.new
    rm -f index.html.old
    cp index.html index.html.old
    mv index.html.new index.html

    # Delete all versions not referenced in the top-level HTML file.
    #
    for vers in \`ls -d $PKG_ID_GLOB | sed "s/$PKG_NAME-//"\`; do
        fgrep -s \$vers versions || rm -rf $PKG_NAME-\$vers
    done

    # Set the symbolic link to the current version
    #
    rm -f $PKG_NAME-current
    ln -s $PKG_ID $PKG_NAME-current
EOF
