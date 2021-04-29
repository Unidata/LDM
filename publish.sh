# Publishes the current LDM source distribution.
#   - Executes a local "make install"
#   - In the FTP directory:
#       - Copies the tarball
#       - Deletes old, bug-fix versions
#   - In the download directory:
#       - Ensures a symbolic link to the tarball
#       - Deletes old, bug-fix symbolic links
#       - Updates the table-of-contents HTML file
#   - In the LDM webpage directory:
#       - Copies the installed documentation
#       - Delete old, bug-fix versions
#       - Updates the symbolic links
#
# Usage:
#       $0 [<host>]
# where:
#       <host>     Name of the host on which the LDM package is made public.
#                  Default is "www" or "www-r".

set -e # Exit on error

if test "$1"; then
    host=$1
else
    host=www
    ps -fu $USER | grep 'ssh upc' | grep -v grep >/dev/null && host=${host}-r
fi

version=`awk 'NR==1{print $1; exit;}' CHANGE_LOG`
tarball=ldm-$version.tar.gz
ftpDir=/web/ftp/pub/ldm
downloadDir=/web/content/downloads/ldm

#Current destination for documentation
webDir=/web/content/software/ldm
#New destination for documentation (wait for Jen's and/or Doug's signal)
#webDir=/web/docs/ldm

versionWebDir=$webDir/ldm-$version

copyToFtpDir()
{
    status=1

    if scp $tarball $host:$ftpDir/$tarball; then
        if ssh -T $host bash --login <<EOF; then
            set -e
            cd $ftpDir
            rm -f ldm.tar.gz
            ln -s $tarball ldm.tar.gz
EOF
            status=0
        fi
    fi

    return $status
}

purgeDir()
{
    dir=${1?Directory not specified}
    ssh -T $host bash --login <<EOF
        set -e
        status=1
        cd $dir
        if ! ls -d ldm-[0-9.]*.tar.gz |
                sed "s/ldm-//" |
                sort -t. -k 1nr,1 -k 2nr,2 -k 3nr,3 |
                awk -F. '\$1!=ma||\$2!=mi{print}{ma=\$1;mi=\$2}' >versions; then
            rm -f versions
        else
            for vers in \`ls -d ldm-[0-9.]*.tar.gz | sed "s/ldm-//"\`; do
                fgrep -s \$vers versions || rm -rf ldm-\$vers
            done
            rm -f versions
            status=0;
        fi
        exit $status
EOF
}

linkToTarball()
{
    ssh -T $host bash --login <<EOF
        set -e
        cd $downloadDir
        rm -f $tarball
        ln -s $ftpDir/$tarball
EOF
}

adjustDownloadHtml()
{
    ssh -T $host bash --login <<EOF
        set -e
        status=1
        cd $downloadDir
        if ! sed "s/ldm-[0-9]\{1,\}\.[0-9]\{1,\}\.[0-9]\{1,\}/ldm-$version/g" \
                index.html >index.html.new; then
            rm -f index.html.new
        else
            cp -f index.html index.html.old
            mv -f index.html.new index.html
            status=0
        fi
        exit $status
EOF
}

copyDoc()
{
    status=1
    ssh -T $host rm -rf $versionWebDir
    if ! scp -Br ../share/doc/ldm $host:$versionWebDir >/dev/null; then
        ssh -T $host rm -rf $versionWebDir
    else
        status=0
    fi
    return $status
}

referenceDoc()
{
    ssh -T $host bash --login <<EOF
        set -e  # Exit on error
        status=1

        # Go to the top-level of the package's web-pages.
        cd $webDir

        # Allow group write access to all created files
        umask 02

        # Set the hyperlink references to the documentation. For a given major
        # and minor version, keep only the latest bug-fix.
        echo Linking to documentation in $host:`pwd`
        if ! ls -d ldm-[0-9.]* |
                sed "s/ldm-//" |
                sort -t. -k 1nr,1 -k 2nr,2 -k 3nr,3 |
                awk -F. '\$1!=ma||\$2!=mi{print}{ma=\$1;mi=\$2}' >versions; then
            rm -f versions
        else
            if ! sed -n '1,/BEGIN VERSION LINKS/p' versions.inc >versions.inc.new; then
                rm -f versions.inc.new
            else
                for vers in \`cat versions\`; do
                    versName=ldm-\$vers
                    cat <<END_VERS >>versions.inc.new
                         <tr>
                          <td>
                           <b>\$vers</b>
                          </td>
                          <td>
                           <a href="\$versName">Documentation</a> 
                          </td>
                          <td>
                           <a href="/downloads/ldm/\$versName.tar.gz">Download</a>
                          </td>
                          <td>
                           <a href="\$versName/CHANGE_LOG">Release Notes</a> 
                          </td>
                         </tr>

END_VERS
                done
                sed -n '/END VERSION LINKS/,\$p' versions.inc >>versions.inc.new
                rm -f versions.inc.old
                cp versions.inc versions.inc.old
                mv versions.inc.new versions.inc

                # Delete all versions not referenced in the top-level HTML file.
                echo Deleting unreferenced version in $host:`pwd`
                for vers in \`ls -d ldm-[0-9.]* | sed "s/ldm-//"\`; do
                    fgrep -s \$vers versions || rm -rf ldm-\$vers
                done

                # Set the symbolic link to the current version
                echo Making ldm-$version the current version
                rm -f ldm-current
                ln -s ldm-$version ldm-current

                status=0
            fi # "versions.inc.new" created

            rm -f versions
        fi # "versions" created

        exit $status
EOF
}

status=1

# Ensure that the package is installed so that the installed documentation can
# be copied to the website.
echo Installing package locally
if make install >&install.log; then

    # Copy the tarball to the FTP directory
    echo Copying $tarball to $host:$ftpDir
    if copyToFtpDir; then

        # Purge the FTP directory of bug-fix versions that are older than the
        # latest corresponding minor release.
        echo Purging $host:$ftpDir of bug-fixes older than $tarball
        if purgeDir $ftpDir; then
        
            # Ensure that the download directory has a link to the tarball
            echo Creating symbolic link to $host:$ftpDir in $host:$downloadDir
            if linkToTarball; then
            
                # Purge the download directory of symbolic links that are older
                # than the latest corresponding minor release.
                echo Purging $host:$downloadDir of bug-fixes older than $tarball
                if purgeDir $downloadDir; then
                
                    # Modify the HTML file in the download directory to
                    # reference the tarball's symbolic link
                    echo Modifying HTML file in $host:$downloadDir 
                    if adjustDownloadHtml; then
                    
                        # Copy the documentation to the package's website
                        echo Copying documentation to $host:$versionWebDir
                        if copyDoc; then
                        
                            # Ensure that the package's home-page references the
                            # just-copied documentation.
                            echo Modifying HTML file in $host:$webDir
                            if referenceDoc; then
                                status=0
                            fi # New documentation referenced
                        fi # Documentation copied to website
                    fi # Download directory's HTML modified
                fi # Download directory purged
            fi # Link from download directory to FTP directory made
        fi # FTP directory purged
    fi # Tarball copied to FTP directory
fi # Package installed

exit $status
