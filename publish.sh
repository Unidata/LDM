# Copies an LDM source distribution to the public download directory and ensures
# the existence of that version's documentation on the package's website. The
# package is built and installed in the current working directory to extract the
# documentation.
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

# Make a documentation installation for copying to the website.
#
pax -zr <$SOURCE_DISTRO_NAME
trap "rm -rf $PKG_ID; `trap -p EXIT`" EXIT
cd $PKG_ID
./configure --prefix=$ABSPATH_DEFAULT_INSTALL_PREFIX --disable-root-actions \
        >configure.log 2>&1
make install DESTDIR=$PKG_ID >install.log 2>&1

# Copy the documentation to the package's website.
#
versionWebDirTmp=$ABSPATH_VERSION_WEB_DIR.tmp
ssh -T $WEB_HOST rm -rf $versionWebDirTmp
trap "ssh -T $WEB_HOST rm -rf $versionWebDirTmp; `trap -p ERR`" ERR
scp -Br $PKG_ID/$ABSPATH_DEFAULT_INSTALL_PREFIX/$RELPATH_DOC_DIR
        $WEB_HOST:$versionWebDirTmp
ssh -T $WEB_HOST mv -f $versionWebDirTmp $ABSPATH_VERSION_WEB_DIR

# Ensure that the package's home-page references the just-installed
# documentation.
ssh -T $WEB_HOST bash --login <<EOF
    set -ex  # Exit on error

    version=`echo $PKG_ID | sed 's/.*-//'`
    indexHtml=index.html

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
        href=\`find $PKG_NAME-\$vers -name index.html\`
        test "\$href" || href=\`find $PKG_NAME-\$vers -name \$indexHtml\`
        echo "            <li><a href=\"\$href\">\$vers</a>" >>index.html.new
    done
    sed -n '/$END_VERSION_LINKS/,\$p' index.html >>index.html.new
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