# Copies a source distribution to the download area and adds the version to the
# website. Does this if and only if all the upstream jobs in the delivery
# pipeline were successful. An upstream job is considered successful if its
# documentation distribution exists.
#
# This script is complicated by the fact that it will be invoked by every
# upstream job.
#
# Usage:
#     $0 pipeId nJobs srcDistroFile docDistroFile pkgName
#
# where:
#     pipeId                Unique identifier for the parent delivery pipeline
#                           instance (e.g., top-of-the-pipe job number)
#     nJobs                 Number of upstream jobs
#     srcDistroFile         Pathname of the source distribution file
#     docDistroFile         Pathname of the documentation distribution file
#     pkgName               Name of the package (e.g., "udunits")

set -e  # exit on failure

pipeId=${1:?Group ID not specified}
nJobs=${2:?Number of upstream jobs not specified}
srcDistroFile=${4:?Source distribution file not specified}
docDistroFile=${6:?Documentation distribution file not specified}
pkgName=${7:?Package name not specified}

srcRepoHost=webserver            # Name of computer hosting source repository
srcRepoDir=/web/ftp/pub/$pkgName # Pathname of source repository
webHost=webserver                # Name of computer hosting package website

# Indicates if the outcome of the upstream jobs is decidable.
#
decidable() {
    test `ls $jobId.success $jobId.failure 2>/dev/null | wc -w` -ge $nJobs
}

# Indicates if the upstream jobs were successful.
#
success() {
    test `ls $jobId.success 2>/dev/null | wc -w` -ge $nJobs
}

# Ensure valid pathnames.
#
srcDistroFile=`ls $srcDistroFile`


# Form a unique identifier for this invocation.
#
jobId=$pipeId-`basename $srcDistroFile`

# Remove any leftovers from an earlier delivery pipeline.
#
ls *.success *.failure 2>/dev/null | grep -v ^$jobId | xargs rm -rf

# Make known to all invocations of this script in the delivery pipeline the
# outcome of the upstream job associated with this invocation.
#
if test -e $docDistroFile; then
    touch $jobId.success
else
    touch $jobId.failure
fi

# Wait until the outcome of all the upstream jobs can be decided.
#
while ! decidable; do
    sleep 3
done

# Set the absolute path to the public source distribution file.
#
srcDistroPath=/web/ftp/pub/$pkgName/`basename $srcDistroFile`

# If the source repository doesn't have the source distribution,
#
if ! ssh $srcRepoHost test -e $srcDistroPath; then
    #
    # Copy the source distribution to the source repository.
    #
    trap "ssh $srcRepoHost rm -f $srcDistroPath; `trap -p ERR`" ERR
    scp $srcDistroFile $srcRepoHost:$srcDistroPath
fi

# Upload the documentation to the package's website.
#
pkgId=`basename $docDistroFile | sed 's/^\([^-]*-[0-9.]*\).*/\1/'`
version=`echo $pkgId | sed 's/^[^-]*-//'`
pkgWebDir=/web/content/software/$pkgName
ssh -T $webHost bash -x $pkgWebDir/upload $version <$docDistroFile
