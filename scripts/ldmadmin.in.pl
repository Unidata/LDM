#!@PERL@
use POSIX;
#
# $Id: ldmadmin.in,v 1.86.2.3.2.2.2.17.2.53 2009/09/04 15:37:18 steve Exp $
#
# File:         ldmadmin
#
# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Description: This perl script provides a command line interface to LDM
#  programs.
#
# Files:
#
#  $LDMHOME/ldm.pid          file containing process group ID
#  $LDMHOME/.ldmadmin.lck    lock file for operations that modify the LDM
#  $LDMHOME/.[0-9a-f]*.info  product-information of the last, successfuly-
#                            received data-product
###############################################################################

###############################################################################
# DEFAULT CONFIGURATION SECTION
###############################################################################
if (! $ENV{'LDMHOME'}) {
    $ENV{'LDMHOME'} = "$ENV{'HOME'}";
}

###############################################################################
# END OF DEFAULT CONFIGURATION
###############################################################################
srand;	# called once at start

# Some parameters used by this script:
$ldmhome = "@LDMHOME@";
$progname = "ldmadmin";
$feedset = "ANY";
chop($os = `uname -s`);
chop($release = `uname -r`);
$begin = 19700101;
$end = 30000101;
$lock_file = "$ldmhome/.ldmadmin.lck";
$pid_file = "$ldmhome/ldmd.pid";
$bin_path = "$ldmhome/bin";
$line_prefix = "";
$pqact_conf_option = 0;

# Ensure that some environment variables are set.
$ENV{'PATH'} = "$bin_path:$ENV{'PATH'}";

# we want a flush after every print statement
$| = 1;

# Get the command. Error if no command specified.
$_ = $ARGV[0];
shift;
$command = $_;
if (!$command) {
    print_usage();
    exit 1;
}
# The "clean" command is checked for here because a locked registry will cause
# this script to hang trying to get parameters from the registry.
if ($command eq "clean") {	# clean up after an abnormal termination
    if (resetRegistry()) {
	exit 4;
    }
}

# Get some configuration parameters from the registry
#
if (!isRunning($pid_file, $ip_addr)) {
    # This is a workaround for problems with the Berkeley DB environment.
    resetRegistry();
}
@regpar = (
    [\$ldmd_conf, "regpath{LDMD_CONFIG_PATH}"],
    [\$q_path, "regpath{QUEUE_PATH}"],
    [\$q_path, "regpath{QUEUE_PATH}"],
    [\$hostname, "regpath{HOSTNAME}"],
    [\$insertion_check_period, "regpath{INSERTION_CHECK_INTERVAL}"],
    [\$pq_size, "regpath{QUEUE_SIZE}"],
    [\$pq_slots, "regpath{QUEUE_SLOTS}"],
    [\$surf_path, "regpath{SURFQUEUE_PATH}"],
    [\$surf_size, "regpath{SURFQUEUE_SIZE}"],
    [\$metrics_file, "regpath{METRICS_FILE}"],
    [\$metrics_files, "regpath{METRICS_FILES}"],
    [\$log_file, "regpath{LOG_FILE}"],
    [\$numlogs, "regpath{LOG_COUNT}"],
    [\$log_rotate, "regpath{LOG_ROTATE}"],
    [\$num_metrics, "regpath{METRICS_COUNT}"],
    [\$ip_addr, "regpath{IP_ADDR}"],
    [\$port, "regpath{PORT}"],
    [\$max_clients, "regpath{MAX_CLIENTS}"],
    [\$max_latency, "regpath{MAX_LATENCY}"],
    [\$offset, "regpath{TIME_OFFSET}"],
    [\$pqact_conf, "regpath{PQACT_CONFIG_PATH}"],
    [\$scour_file, "regpath{SCOUR_CONFIG_PATH}"],
    [\$check_time , "regpath{CHECK_TIME}"],
    [\$warn_if_check_time_disabled, "regpath{WARN_IF_CHECK_TIME_DISABLED}"],
    [\$ntpdate, "regpath{NTPDATE_COMMAND}"],
    [\$ntpdate_timeout, "regpath{NTPDATE_TIMEOUT}"],
    [\$time_servers, "regpath{NTPDATE_SERVERS}"],
    [\$check_time_limit, "regpath{CHECK_TIME_LIMIT}"],
    [\$netstat, "regpath{NETSTAT_COMMAND}"],
    [\$top, "regpath{TOP_COMMAND}"],
    [\$delete_info_files, "regpath{DELETE_INFO_FILES}"],
);
for my $entryRef (@regpar) {
    ${$entryRef->[0]} = `regutil $entryRef->[1]` || die;
    chop(${$entryRef->[0]});
}
@time_servers = split(/\s+/, $time_servers);

while ($_ = $ARGV[0]) {
    shift;
    /^([a-z]|[A-Z]|\/)/ && ($ldmd_conf = $_);
    /^-b/ && ($begin = shift);
    /^-e/ && ($end = shift);
    /^-q/ && ($q_path = shift);
    /^-s/ && ($q_size = shift);
    if (/^-C/) {
	$configurationFilePath = shift;
	require $configurationFilePath;
    }
    /^-c/ && $pq_clobber++;
    /^-f/ && $pq_fast++;
    /^-v/ && $verbose++;
    /^-x/ && ($debug++, $verbose++);
    /^-M/ && ($max_clients = shift);
    /^-m/ && ($max_latency = shift);
    /^-n/ && ($numlogs = shift);
    /^-o/ && ($offset = shift);
    /^-P/ && ($port = shift);
    /^-l/ && ($log_file = shift);
    /^-f/ && ($feedset = shift);
    /^-p/ && ($pqact_conf = shift, $pqact_conf_option = 1);
}

# Check the hostname for a fully-qualified version.
#
if ($hostname !~ /\./) {
    bad_exit("The LDM-hostname is not fully-qualified.  " . 
        "Execute the command \"regutil -s <hostname> regpath{HOSTNAME}\" ".
        "to set the fully-qualified name of the host.")
}

# Change the current working directory to the home directory.  This will prevent
# core files from being created all over the place.
#
chdir $ldmhome;

#
# process the command request
#
if ($command eq "start") {	# start the ldm
    $status = start_ldm();
}
elsif ($command eq "stop") {	# stop the ldm
    $status = stop_ldm();
}
elsif ($command eq "restart") {	# restart the ldm
    $status = stop_ldm();
    if (!$status) {
	$status = start_ldm();
    }
}
elsif ($command eq "mkqueue") {	# create a product queue using pqcreate(1)
    $status = make_pq();
}
elsif ($command eq "delqueue") { # delete a product queue
    $status = delete_pq();
    if ($status == 0 && $delete_info_files) {
	unlink <.*.info>;
    }
}
elsif ($command eq "mksurfqueue") { # create a product queue for pqsurf(1)
    $status = make_surf_pq();
}
elsif ($command eq "delsurfqueue") { # delete a pqsurf product queue
    $status = del_surf_pq();
}
elsif ($command eq "newlog") {	# rotate the log files
    if (0 == ($status = make_lockfile())) {
        $status = new_log();
        rm_lockfile();
    }
}
elsif ($command eq "scour") {	# scour data directories
    system("scour $scour_file");
    $status = $?;
}
elsif ($command eq "isrunning") { # check if the ldm is running
    $status = !isRunning($pid_file, $ip_addr);
}
elsif ($command eq "checkinsertion") { # check if a product has been inserted
    $status = check_insertion();
}
elsif ($command eq "vetqueuesize") { # vet the size of the queue
    $status = vetQueueSize();
}
elsif ($command eq "check") {	# check the LDM system
    $status = check_ldm();
}
elsif ($command eq "watch") {	# monitor incoming products
    if (!isRunning($pid_file, $ip_addr)) {
	bad_exit("There is no LDM server running");
    }
    system("pqutil -r -f \"$feedset\" -w $q_path");
}
elsif ($command eq "pqactcheck") {	# check pqact file for errors
    $status = !are_pqact_confs_ok();
}
elsif ($command eq "pqactHUP") {	# HUP pqact 
    ldmadmin_pqactHUP();
}
elsif ($command eq "queuecheck") {	# check queue for corruption 
    if (isRunning($pid_file, $ip_addr)) {
	bad_exit("queuecheck: The LDM system is running. queuecheck aborted");
    }
    $status = !isProductQueueOk();
}
elsif ($command eq "config") {	# show the ldm configuration
    $status = ldm_config();
}
elsif ($command eq "log") {	# do a "more" on the logfile
    system("more","$log_file");
    $status = $?;
}
elsif ($command eq "tail") {	# do a "tail -f" on the logfile
    system("tail","-f","$log_file");
    $status = $?;
}
elsif ($command eq "clean") {	# clean up after an abnormal termination
    if (isRunning($pid_file, $ip_addr)) {
	errmsg("The LDM system is running!  Stop it first.");
	$status = 1;
    }
    elsif (unlink($lock_file) == 0) {
	errmsg("Couldn't remove ldmadmin(1) lock-file \"$lock_file\"");
	$status = 2;
    }
    elsif (unlink($pid_file) == 0) {
	errmsg("Couldn't remove LDM server PID-file \"$pid_file\"");
	$status = 3;
    }
    else {
	$status = 0;
    }
}
elsif ($command eq "checktime") {
    print "Checking accuracy of system clock ... ";
    $check_time = 1;
    if (checkTime()) {
        print "\n";
	$status = 1;
    }
    else {
	print "OK\n";
    }
}
elsif ($command eq "printmetrics") {
    $status = printMetrics();
}
elsif ($command eq "addmetrics") {
    $status = system("ldmadmin printmetrics >>$metrics_file");
}
elsif ($command eq "plotmetrics") {
    $status = plotMetrics();
}
elsif ($command eq "newmetrics") {
    $status = system("newlog $metrics_file $num_metrics");
}
elsif ($command eq "usage") {	# print usage message
    print_usage();
    $status = 0;
}
else {				# bad command
    errmsg("Unknown command: \"$command\"");
    print_usage();
    $status = 1;
}
#
# that's all folks
#
exit $status;

###############################################################################
# bad_exit error routine.  Writes error to both stderr and via syslogd.
###############################################################################

sub bad_exit
{
    my($err_str) = @_;
    my($date_str) = get_date();

# remove the lockfile if it exists
    if (-e $lock_file) {
	rm_lockfile();
    }
# output to standard error
    errmsg("$date_str $hostname $progname[$<]: $err_str");

# exit with extreme prejudice
    exit 1;
}

###############################################################################
# Date Routine.  Gets data and time as GMT in the same format as the LDM log
# file.
###############################################################################

sub get_date
{
    @month_array = (Jan,Feb,Mar,Apr,May,Jun,Jul,Aug,Sep,Oct,Nov,Dec);
 
    my($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) =
        gmtime(time());
 
    my($date_string) =
        sprintf("%s %d %02d:%02d:%02d UTC", $month_array[$mon], $mday,
                $hour, $min,$sec);
 
    return $date_string;
}

###############################################################################
# Print a usage message and exit.  Should only be called when the command is
# usage, or command line arguments are bad or missing.
###############################################################################

sub print_usage
{
    print "Usage: $progname command [options] [conf_file]";
    print "\n\ncommands:";
    print "\n\tstart [-v] [-x] [-m maxLatency] [-o offset] [-q q_path] [-M max_clients]\n" .
	"\t\t\t\t\t\tStart the LDM";
    print "\n\tstop\t\t\t\t\tStop the LDM";
    print "\n\trestart [-v] [-x] [-m maxLatency] [-o offset] [-q q_path] [-M max_clients]\n" .
	"\t\t\t\t\t\tRestart a running LDM";
    print "\n\tmkqueue [-v] [-x] [-c] [-f] [-q q_path]\n" .
	"\t\t\t\t\t\tCreate a product queue";
    print "\n\tdelqueue [-q q_path]\t\t\tDelete a product queue";
    print "\n\tmksurfqueue [-v] [-x] [-c] [-f] [-q q_path]\n" .
	"\t\t\t\t\t\tCreate a product queue for \n" .
	"\t\t\t\t\t\t  pqsurf";
    print "\n\tdelsurfqueue [-q q_path]\t\tDelete a pqsurf product queue";
    print "\n\tnewlog [-n numlogs] [-l logfile]\tRotate a log file";
    print "\n\tscour\t\t\t\t\tScour data directories";
    print "\n\tisrunning\t\t\t\tExit status 0 if LDM is running,";
    print "\n\t\t\t\t\t\t  else exit 1";
    print "\n\tcheckinsertion\t\t\t\tCheck for recent insertion of";
    print "\n\t\t\t\t\t\tdata-product into product-queue";
    print "\n\tvetqueuesize\t\t\t\tVet the size of the queue";
    print "\n\tpqactcheck [-p pqact_conf]\t\tCheck syntax for pqact files";
    print "\n\tpqactHUP\t\t\t\tSend HUP signal to pqact program";
    print "\n\tqueuecheck\t\t\t\tCheck for queue corruption";
    print "\n\twatch [-f feedpat]\t\t\tMonitor incoming products";
    print "\n\tconfig\t\t\t\t\tPrint LDM configuration";
    print "\n\tlog\t\t\t\t\tPage through the LDM log file";
    print "\n\ttail\t\t\t\t\tMonitor the LDM log file";
    print "\n\tchecktime\t\t\t\tChecks the system clock";
    print "\n\tclean\t\t\t\t\tCleans up after an abnormal termination";
    print "\n\tprintmetrics\t\t\t\tPrints LDM metrics";
    print "\n\taddmetrics\t\t\t\tAccumulates LDM metrics";
    print "\n\tplotmetrics [-b begin] [-e end]\t\tPlots LDM metrics";
    print "\n\tnewmetrics\t\t\t\tRotates the metrics files";
    print "\n\tusage\t\t\t\t\tThis message\n";
    print "\n\noptions:";
    print "\n\t-b begin\tBegin time as YYYYMMDD[.hh[mm[ss]]]";
    print "\n\t-C path\t\tConfiguration-file for this utility";
    print "\n\t\t\t  Default: $configurationFilePath";
    print "\n\t-c\t\tClobber an exisiting product queue";
    print "\n\t-e end\t\tEnd time as YYYYMMDD[.hh[mm[ss]]]";
    print "\n\t-f feedset\tFeed set to use with command";
    print "\n\t\t\t  Default: $feedset";
    print "\n\t\t\t  If more than one word, enclose string in double quotes";
    print "\n\t\t\t  Default is user name, tty, login time";
    print "\n\t-l logfile\tName of logfile";
    print "\n\t\t\t  Default: $log_file";
    print "\n\t-m maxLatency\tConditional data-request temporal-offset";
    print "\n\t-M max_clients\tMaximum number of active clients";
    print "\n\t-n numlogs\tNumber of logs to rotate";
    print "\n\t\t\t  Default: $numlogs";
    print "\n\t-o offset\tUnconditional data-request temporal-offset";
    print "\n\t-q q_path\tSpecify a product queue path";
    print "\n\t\t\t  LDM Default: $q_path";
    print "\n\t\t\t  pqsurf Default: $surf_path";
    print "\n\t-v\t\tTurn on verbose mode";
    print "\n\t-x\t\tTurn on debug mode (includes verbose mode)";
    print "\n\nconf_file:";
    print "\n\twhich LDM configuration-file file to use";
    print "\n\t    Default: $ldmd_conf";
    print "\n";
}

# Resets the LDM registry.
#
# Returns:
#       0               Success.
#       else            Failure.  "errmsg()" called.
#
sub resetRegistry
{
    my $status = 1;     # default failure

    if (system("regutil -R")) {
	errmsg("Couldn't reset LDM registry");
    }
    else {
        $status = 0;
    }

    return $status;
}

###############################################################################
# check for the existence of the lock file.  Exit if found, create if not
# found.
###############################################################################

sub make_lockfile
{
    my $status = 1;     # default failure

    if (-e $lock_file) {
	errmsg("make_lockfile(): another ldmadmin(1) process exists");
    }
    else {
        if (!open(LOCKFILE,">$lock_file")) {
            errmsg("make_lockfile(): Cannot open lock file $lock_file");
        }
        else {
            close(LOCKFILE);
            $status = 0;
        }
    }

    return $status;
}

###############################################################################
# remove a lock file. exit if not found.
###############################################################################

sub rm_lockfile
{
    if (-e $lock_file) {
	unlink($lock_file);
    }
    else {
	bad_exit("rm_lockfile: Lock file does not exist");
    }

}

###############################################################################
# create a product queue
###############################################################################

sub make_pq
{
    my $status = 1;     # default failure

    if ($q_size) {
	errmsg("product queue -s flag not supported, no action taken.");
    }
    else {
        # Create the lock file
        if (0 == ($status = make_lockfile())) {
            # Ensure the LDM system isn't running
            if (isRunning($pid_file, $ip_addr)) {
                errmsg("make_pq(): There is a server running, mkqueue aborted");
            }
            else {
                # Build the command line
                $cmd_line = "pqcreate";
                $cmd_line .= " -x" if ($debug);
                $cmd_line .= " -v" if ($verbose);
                $cmd_line .= " -c" if ($pq_clobber);
                $cmd_line .= " -f" if ($pq_fast);
                $cmd_line .= " -S $pq_slots" if ($pq_slots ne "default");
                $cmd_line .= " -q $q_path -s $pq_size";

                # execute pqcreate(1)
                if (system("$cmd_line")) {
                    errmsg("make_pq(): mkqueue(1) failed");
                }
                else {
                    $status = 0;
                }
            }                           # LDM system not running

            # remove the lockfile
            rm_lockfile();
        }                               # lock file created
    }

    return 0;
}

###############################################################################
# delete a product queue
###############################################################################

sub delete_pq
{
    my $status = 1;     # default failure

    # Create the lock file
    if (0 == ($status = make_lockfile())) {
        # Check to see if the server is running.  Exit if it is.
        if (isRunning($pid_file, $ip_addr)) {
            errmsg("delete_pq(): A server is running, cannot delete the queue");
        }
        else {
            # Delete the queue
            if (! -e $q_path) {
                errmsg("delete_pq(): Product-queue \"$q_path\" doesn't exist");
            }
            else {
                unlink($q_path);
                $status = 0;
            }
        }

        # remove the lock file
        rm_lockfile();
    }                                   # lock file created

    return $status;
}

###############################################################################
# create a pqsurf product queue
###############################################################################

sub make_surf_pq
{
    my $status = 1;                     # default failure

    if ($q_size) {
	errmsg("product queue -s flag not supported, no action taken.");
    }
    else {
        # lock file check
        if (0 == ($status = make_lockfile())) {
            # can't do this while there is a server running
            if (isRunning($pid_file, $ip_addr)) {
                errmsg("make_surf_pq(): There is a server running, ".
                    "mkqueue aborted");
            }
            else {
                # set path if necessary
                if ($q_path) {
                    $surf_path = $q_path;
                }

                # need the number of slots to create
                $surf_slots = $surf_size / 1000000 * 6881;

                # build the command line
                $cmd_line = "pqcreate";

                if ($debug) {
                    $cmd_line .= " -x";
                }
                if ($verbose) {
                    $cmd_line .= " -v";
                }

                if ($pq_clobber) {
                    $cmd_line .= " -c";
                }

                if ($pq_fast) {
                    $cmd_line .= " -f";
                }

                $cmd_line .= " -S $surf_slots -q $surf_path -s $surf_size";

                # execute pqcreate
                if (system("$cmd_line")) {
                    errmsg("make_surf_pq(): pqcreate(1) failure");
                }
                else {
                    $status = 0;
                }
            }

            # Remove the lockfile
            rm_lockfile();
        }
    }

    return $status;
}

###############################################################################
# delete a pqsurf product queue
###############################################################################

sub del_surf_pq
{
    my $status = 1;                     # default failure

    # lock file check
    if (0 == ($status = make_lockfile())) {
        # check to see if the server is running.  Exit if it is
        if (isRunning($pid_file, $ip_addr)) {
            errmsg("del_surf_pq: A server is running, cannot delete the queue");
        }
        else {
            # check for the queue path
            if ($q_path) {
                $surf_path = $q_path;
            }

            # delete the queue
            if (! -e $surf_path) {
                errmsg("del_surf_pq(): $surf_path does not exist");
            }
            else {
                unlink($surf_path);
                $status = 0;
            }
        }

        # remove the lock file
        rm_lockfile();
    }                                   # lock file created

    return 0;
}

###############################################################################
# start the LDM server
###############################################################################

sub start
{
    my $status = 0;     # default success

    # Build the command line
    $cmd_line = "ldmd -I $ip_addr -P $port -M $max_clients -m $max_latency ".
        "-o $offset -q $q_path";

    if ($debug) {
        $cmd_line .= " -x";
    }
    if ($verbose) {
        $cmd_line .= " -v";
    }

    # Check the ldm(1) configuration-file
    print "Checking LDM configuration-file ($ldmd_conf)...\n";
    my $prev_line_prefix = $line_prefix;
    $line_prefix .= "    ";
    ( @output ) = `$cmd_line -nl- $ldmd_conf 2>&1` ;
    if ($?) {
        errmsg("start(): Problem with LDM configuration-file:\n".
            "@output");
        $status = 1;
    }
    else {
        $line_prefix = $prev_line_prefix;

        print "Starting the LDM server...\n";
        system("$cmd_line $ldmd_conf > $pid_file");
        if ($?) {
            unlink($pid_file);
            errmsg("start(): Could not start LDM server");
            $status = 1;
        }
        else {
            # Check to make sure the LDM is running
            my($loopcount) = 1;
            while(!isRunning($pid_file, $ip_addr)) {
                if($loopcount > 15) {
                    errmsg("start(): ".
                        "Server not started.");
                    $status = 1;        # failure
                    break;
                }
                sleep($loopcount);
                $loopcount++;
            }
        }
    }

    return $status;
}

sub start_ldm
{
    my $status = 0;     # default success

    # Create the lockfile
    #print "start_ldm(): Creating lockfile\n";
    if (0 == ($status = make_lockfile())) {
        # Make sure there is no other server running
        #print "start_ldm(): Checking for running LDM\n";
        if (isRunning($pid_file, $ip_addr)) {
            errmsg("start_ldm(): There is another server running, ".
                "start aborted");
            $status = 1;
        }
        else {
            #print "start_ldm(): Checking for PID-file\n";
            if (-e $pid_file) {
                errmsg("start_ldm(): PID-file \"$pid_file\" exists.  ".
                    "Verify that all is well and then execute ".
                    "\"ldmadmin clean\" to remove the PID-file.");
                $status = 1;
            }
            else {
                # Check the product-queue
                #print "start_ldm(): Checking product-queue\n";
                if (!isProductQueueOk())  {
                    errmsg("LDM not started");
                    $status = 1;
                }
                else {
                    # Check the pqact(1) configuration-file(s)
                    print "Checking pqact(1) configuration-file(s)...\n";
                    my $prev_line_prefix = $line_prefix;
                    $line_prefix .= "    ";
                    if (!are_pqact_confs_ok()) {
                        errmsg("");
                        $status = 1;
                    }
                    else {
                        $line_prefix = $prev_line_prefix;

                        # Rotate the ldm log files if appropriate
                        if ($log_rotate) {
                            #print "start_ldm(): Rotating log files\n";
                            if (new_log()) {
                                errmsg("start_ldm(): ".
                                    "Couldn't rotate log files");
                                $status = 1;
                            }
                        }

                        if (0 == $status) {
                            $status = start();
                        }
                    }                   # pqact(1) config-files OK
                }                       # product-queue OK
            }                           # PID-file doesn't exist
        }                               # LDM not running

        # Remove the lockfile
        rm_lockfile();
    }                                   # lock file created

    return $status;
}

###############################################################################
# stop the LDM server
###############################################################################

sub stop_ldm
{
    my $status = 0;                     # default success

    # create the lockfile
    if (0 == ($status = make_lockfile())) {
        # get pid 
        $rpc_pid = getPid($pid_file) ;

        if ($rpc_pid == -1) {
            errmsg("The LDM isn't running or its process-ID is unavailable");
            $status = 1;
        }
        else {
            # Flush system I/O buffers to disk.
            print "Flushing the LDM product-queue to disk...\n";
            system( "sync" );
            sleep(1);

            # kill the server and associated processes
            print "Stopping the LDM server...\n";
            system( "kill $rpc_pid" );

            # we may need to sleep to make sure that the port is deregistered
            my($loopcount) = 1;
            while(isRunning($pid_file, $ip_addr)) {
                if($loopcount > 65) {
                    bad_exit("stop_ldm: Server not dead.");
                    $status = 1;
                }
                print "Waiting for the LDM to terminate...\n" ;
                sleep($loopcount);
                $loopcount++;
            }
        }

        # remove product-information files that are older than the LDM pid-file.
        removeOldProdInfoFiles();

        # get rid of the pid file
        unlink($pid_file);

        # remove the lockfile
        rm_lockfile();
    }

    return $status;
}

###############################################################################
# rotate the specified log file, keeping $numlog files
###############################################################################

sub new_log
{
    my $status = 1;      # default failure

    # Rotate the log file
    system("newlog $log_file $numlogs");

    # If rotation successful, notify syslogd(8)
    if ($?) {
	errmsg("new_log(): log rotation failed");
    }
    else {
	system("hupsyslog");

        if ($?) {
            errmsg("new_log(): Couldn't notify system logging daemon");
        }
        else {
            $status = 0;        # success
        }
    }

    return $status;
}

###############################################################################
# print the LDM configuration information
###############################################################################

sub ldm_config
{
    print  "\n";
    print  "hostname:              $hostname\n";
    print  "os:                    $os\n";
    print  "release:               $release\n";
    print  "ldmhome:               $ldmhome\n";
    print  "LDM version:           @VERSION@\n";
    print  "PATH:                  $ENV{'PATH'}\n";
    print  "log file:              $log_file\n";
    print  "LDM conf file:         $ldmd_conf\n";
    print  "pqact(1) conf file:    $pqact_conf\n";
    print  "scour(1) conf file:    $scour_file\n";
    print  "product queue:         $q_path\n";
    print  "queue size:            $pq_size bytes\n";
    print  "queue slots:           $pq_slots\n";
    print  "pqsurf(1) path:        $surf_path\n";
    print  "pqsurf(1) size:        $surf_size\n";
    printf "IP address:            %s\n", length($ip_addr) ? $ip_addr : "all";
    printf "port:                  %d\n", length($port) ? $port : @LDM_PORT@; 
    print  "PID file:              $pid_file\n";
    print  "Lock file:             $lock_file\n";
    print  "maximum clients:       $max_clients\n";
    print  "maximum latency:       $max_latency\n";
    print  "time offset:           $offset\n";
    print  "log file:              $log_file\n";
    print  "numlogs:               $numlogs\n";
    print  "log_rotate:            $log_rotate\n";
    print  "netstat:               $netstat\n";
    print  "top:                   $top\n";
    print  "metrics file:          $metrics_file\n";
    print  "metrics files:         $metrics_files\n";
    print  "num_metrics:           $num_metrics\n";
    print  "check time:            $check_time\n";
    print  "delete info files:     $delete_info_files\n";
    print  "ntpdate(1):            $ntpdate\n";
    print  "ntpdate(1) timeout:    $ntpdate_timeout\n";
    print  "time servers:          ", join(" ", @time_servers), "\n";
    print  "time-offset limit:     $check_time_limit\n";
    print "\n";

    return 0;
}

###############################################################################
# check if the LDM is running.  return 1 if running, 0 if not.
###############################################################################

sub isRunning
{
    my $pid_file = $_[0];
    my $ip_addr = $_[1];
    my($running) = 0;
    my($pid) = getPid($pid_file);

    if ($pid != -1) {
	system("kill -0 $pid > /dev/null 2>&1");
	$running = !$?;
    }

    if (!$running) {
	my($cmd_line) = "ldmping -l- -i 0";
	$cmd_line = $cmd_line . " $ip_addr" if $ip_addr ne "0.0.0.0";

	system("$cmd_line > /dev/null 2>&1");
	$running = !$?;
    }

    return $running;
}

###############################################################################
# Check that a data-product has been inserted into the product-queue
###############################################################################

sub check_insertion
{
    my $status = 1;                     # default failure
    chomp(my($line) = `pqmon -S -q $q_path`);

    if ($?) {
        errmsg("check_insertion(): pqmon(1) failure");
    }
    else {
        my @params = split(/\s+/, $line);
        my $age = $params[8];

        if ($age > $insertion_check_period) {
            errmsg("check_insertion(): The last data-product was inserted ".
                "$age seconds ago, which is greater than the configuration-".
                "parameter \"regpath{INSERTION_CHECK_INTERVAL}\" ".
                "($insertion_check_period seconds).");
        }
        else {
            $status = 0;
        }
    }

    return $status;
}

###############################################################################
# Check the size of the queue.
###############################################################################

sub grow
{
    my $oldQueuePath = $_[0];
    my $newQueuePath = $_[1];
    my $status = 1;                     # failure default;

    print "Starting restricted LDM...\n";
    my $cmd = "ldmd -I 127.0.0.1 -P $port -m $max_latency -q $newQueuePath ".
            "$ldmd_conf.grow >$pid_file &";
    if (system("$cmd")) {
        errmsg("grow(): Couldn't start restricted LDM");
    }
    else {
        my $restrictedLdmRunning = 1;

        print "Copying products from old queue to new ".
            "queue...\n";
        if (system("pqsend -h 127.0.0.1 -i 0 -o $max_latency ".
                "-q $oldQueuePath")) {
            errmsg("grow(): Couldn't copy products");
        }
        else {
            print "Stopping restricted LDM...\n";
            if (0 != ($status = stop_ldm())) {
                errmsg("grow(): Couldn't stop restricted LDM");
            }
            else {
                print "Renaming old queue\n";
                if (system("mv -f $oldQueuePath $oldQueuePath.old")) {
                    errmsg("grow(): Couldn't rename old queue");
                }
                else {
                    print "Renaming new queue\n";
                    if (system("mv $newQueuePath $oldQueuePath")) {
                        errmsg("grow(): Couldn't rename new queue");
                    }
                    else {
                        $restrictedLdmRunning = 0;

                        if ($status) {
                            print "Deleting new queue\n";
                            if (unlink($oldQueuePath) == 0) {
                                errmsg("grow(): Couldn't delete new queue");
                            }
                        }
                    }                   # new queue renamed

                    if ($status) {
                        print "Restoring old queue\n";
                        if (system("mv -f $oldQueuePath.old $oldQueuePath")) {
                            errmsg("grow(): Couldn't restore old queue");
                        }
                    }
                }                       # old queue renamed
            }                           # restricted LDM stopped
        }                               # products copied

        if ($status && $restrictedLdmRunning) {
            print "Stopping the restricted LDM\n";
            if (stop_ldm()) {
                errmsg("grow(): Couldn't stop restricted LDM");
            }
        }
    }                                   # restricted LDM started

    return $status;
}

sub saveQueuePar
{
    my $size = $_[0];
    my $slots = $_[1];
    my $status = 1;                     # failure default

    if (system("regutil -u $size regpath{QUEUE_SIZE}")) {
        errmsg("saveQueuePar(): Couldn't save new queue size");
    }
    else {
        if (system("regutil -u $slots regpath{QUEUE_SLOTS}")) {
            errmsg("saveQueuePar(): Couldn't save queue slots");

            print "Restoring previous queue size\n";
            if (system("regutil -u $pq_size regpath{QUEUE_SIZE}")) {
                errmsg("saveQueuePar(): Couldn't restore previous queue size");
            }
        }
        else {
            $pq_size = $size;
            $pq_slots = $slots;
            $status = 0;                # success
        }
    }

    return $status;
}

sub saveTimePar
{
    my $newTimeOffset = $_[0];
    my $newMaxLatency = $_[1];
    my $status = 1;                     # failure default

    if (system("regutil -u $newTimeOffset regpath{TIME_OFFSET}")) {
        errmsg("saveTimePar(): Couldn't save new time-offset");
    }
    else {
        if (system("regutil -u $newMaxLatency regpath{MAX_LATENCY}")) {
            errmsg("saveTimePar(): Couldn't save new maximum acceptible ".
                "latency");

            print "Restoring previous time-offset\n";
            if (system("regutil -u $offset regpath{TIME_OFFSET}")) {
                errmsg("saveTimePar(): Couldn't restore previous time-offset");
            }
        }
        else {
            $offset = $newTimeOffset;
            $max_latency = $newMaxLatency;
            $status = 0;                # success
        }
    }

    return $status;
}

sub vetQueueSize
{
    my $increaseQueue = "increase queue";
    my $decreaseMaxLatency = "decrease maximum latency";
    my $doNothing = "do nothing";
    my $status = 1;                     # failure default
    chomp(my $line = `pqmon -S -q $q_path`);

    if ($?) {
        errmsg("vetQueueSize(): pqmon(1) failure");
    }
    else {
        my @params = split(/\s+/, $line);
        my $isFull = $params[0];
        my $minVirtResTime = $params[9];

        if (!$isFull || $minVirtResTime >= $offset) {
            $status = 0;
        }
        else {
            errmsg("vetQueueSize(): The maximum acceptible latency ".
                "(configuration parameter \"regpath{MAX_LATENCY}\": ".
                "$max_latency seconds) is greater ".
                "than the observed minimum virtual residence time of ".
                "data-products in the queue ($minVirtResTime seconds).  This ".
                "will hinder detection of duplicate data-products.");

            chomp(my $reconMode = `regutil regpath{RECONCILIATION_MODE}`);
            print "The value of the ".
                "\"regpath{RECONCILIATION_MODE}\" configuration parameter is ".
                "\"$reconMode\"\n";
            if ($reconMode eq $increaseQueue) {
                print "Increasing the capacity of the queue...\n";

                if (0 >= $minVirtResTime) {
                    # Use age of oldest product, instead
                    $minVirtResTime = $params[7];
                }
                if (0 >= $minVirtResTime) {
                    # Ensure that the divisor isn't zero
                    $minVirtResTime = 1;
                }
                my $ratio = $offset/$minVirtResTime + 0.1;
                my $newByteCount = int($ratio*$params[3]);
                my $newSlotCount = int($ratio*$params[6]);
                my $newQueuePath = "$q_path.new";

                print "Creating new queue of $newByteCount ".
                    "bytes and $newSlotCount slots...\n";
                if (system("pqcreate -c -S $newSlotCount -s $newByteCount ".
                        "-q $newQueuePath")) {
                    errmsg("vetQueueSize(): Couldn't create new queue: ".
                        "$newQueuePath");
                }
                else {
                    my $restartNeeded;
                    $status = 0;

                    if (isRunning($pid_file, $ip_addr)) {
                        $restartNeeded = 1;
                        print "Stopping the LDM...\n";
                        if (0 != ($status = stop_ldm())) {
                            errmsg("vetQueueSize(): Couldn't stop the LDM");
                        }
                    }
                    if (0 == $status) {
                        if (0 == ($status = grow($q_path, $newQueuePath))) {
                            print "Saving new queue parameters...\n";
                            $status =
                                saveQueuePar($newByteCount, $newSlotCount);
                        }

                        if ($restartNeeded) {
                            print "Restarting original LDM...\n";
                            if ($status = start_ldm()) {
                                errmsg("vetQueueSize(): ".
                                    "Couldn't restart original LDM");
                            }
                        }
                    }                   # LDM stopped
                }                       # new queue created
            }                           # mode is increase queue
            elsif ($reconMode eq $decreaseMaxLatency) {
                print "Decreasing the maximum acceptible ".
                    "latency and the time-offset of requests (configuration ".
                    "parameters \"regpath{MAX_LATENCY}\" and ".
                    "\"regpath{TIME_OFFSET}\")...\n";

                if (0 >= $minVirtResTime) {
                    # Use age of oldest product, instead
                    $minVirtResTime = $params[7];
                }
                $minVirtResTime = 1 if (0 >= $minVirtResTime);
                my $ratio = $minVirtResTime/$max_latency;
                my $newMaxLatency = int($ratio*$max_latency);
                my $newTimeOffset = $newMaxLatency;

                print "New time-offset and maximum latency: ".
                    "$newTimeOffset seconds\n";
                print "Saving new time parameters...\n";
                if (0 == ($status = saveTimePar($newTimeOffset,
                        $newMaxLatency))) {
                    if (isRunning($pid_file, $ip_addr)) {
                        print "Restarting the LDM...\n";
                        if ($status = stop_ldm()) {
                            errmsg("vetQueueSize(): Couldn't stop LDM");
                        }
                        else {
                            if ($status = start_ldm()) {
                                errmsg("vetQueueSize(): Couldn't start LDM");
                            }
                        }               # LDM stopped
                    }                   # LDM is running
                }                       # new time parameters saved
            }                           # mode is decrease max latency
            elsif ($reconMode eq $doNothing) {
                print "Doing nothing.  You should consider setting ".
                    "configuration parameter \"regpath{RECONCILIATION_MODE}\" ".
                    "to \"$increaseQueue\" or \"$decreaseMaxLatency\" or ".
                    "recreate the queue yourself.\n";
            }
            else {
                errmsg("Unknown reconciliation mode: \"$reconMode\"");
            }
        }
    }

    return $status;
}

###############################################################################
# Check the LDM system.
###############################################################################

sub check_ldm
{
    print "Checking for a running LDM system...\n";
    if (!isRunning($pid_file, $ip_addr)) {
        errmsg("The LDM server is not running");
        return 1;
    }

    print "Checking the most-recent insertion into the queue...\n";
    if (check_insertion()) {
        return 2;
    }

    print "Vetting the size of the queue versus the maximum acceptible ".
        "latency...\n";
    if (vetQueueSize()) {
        return 3;
    }

    print "Checking the system clock...\n";
    if (checkTime()) {
        return 4;
    }

    return 0;
}

###############################################################################
# get PID number.  return pid or -1
###############################################################################

sub getPid
{
    my $pid_file = $_[0];
    my( $i, @F, $pid_num ) ;

    if (-e $pid_file) {
	    open(PIDFILE,"<$pid_file");
	    $pid_num = <PIDFILE>;
	    chomp( $pid_num );
	    close( PIDFILE ) ;
	    return $pid_num if( $pid_num =~ /^\d{1,6}/ ) ;
    }
    return -1;
}

###############################################################################
# Check the pqact.conf file(s) for errors
###############################################################################

sub are_pqact_confs_ok
{
    my $are_ok = 1;
    my @pathnames = ();

    if ($pqact_conf_option) {
	# A "pqact" configuration-file was specified on the command-line.
	@pathnames = ($pqact_conf);
    }
    else {
	# No "pqact" configuration-file was specified on the command-line.
	# Set "@pathnames" according to the "pqact" configuration-files
	# specified in the LDM configuration-file.
	if (!open(LDM_CONF_FILE, "<$ldmd_conf")) {
	    bad_exit("Could not open LDM configuration-file, $ldmd_conf");
	}
	else {
	    while (<LDM_CONF_FILE>) {
		if (/^exec/i && /pqact/) {
		    chomp;
		    s/^exec\s+"\s*//i;
		    s/\s*"\s*$//;

		    my @fields = split;
		    my $pathname;

		    if (($#fields == 0) ||
			    ($fields[$#fields] =~ /^-/) ||
			    ($fields[$#fields-1] =~ /^-[ldqfpito]/)) {
		    	$pathname = $pqact_conf;
		    }
		    else {
			$pathname = $fields[$#fields];
		    }
		    @pathnames = (@pathnames, $pathname);
		}
	    }

	    close(LDM_CONF_FILE);
	}
    }

    for my $pathname (@pathnames) {
	# Examine the "pqact" configuration-file for leading spaces.
	my @output;
	my $leading_spaces = 0;

	print "$line_prefix$pathname: ";

	( @output ) = `grep -n "^ " $pathname 2> /dev/null` ;
	if ($#output >= 0) {
	    print "remove leading spaces in the following:\n" ;

	    my $prev_line_prefix = $line_prefix;
	    $line_prefix .= "    ";

	    for my $line (@output) {
		print "$line_prefix$line";
	    }

	    $line_prefix = $prev_line_prefix;
	    $leading_spaces = 1;
	}

	if ($leading_spaces) {
	    $are_ok = 0;
	}
	else {
	    # Check the syntax of the "pqact" configuration-file via "pqact".
	    my $read_ok = 0;

	    ( @output ) = `pqact -vl - -q /dev/null $pathname 2>&1` ;

	    for my $line (@output) {
		if ($line =~ /Successfully read/) {
		    $read_ok = 1;
		    last;
		}
	    }

	    if ($read_ok) {
		print "syntactically correct\n" ;
	    }
	    else {
		print "has problems:\n" ;

		my $prev_line_prefix = $line_prefix;
		$line_prefix .= "    ";

		for my $line (@output) {
		    print "$line_prefix$line";
		}

		$line_prefix = $prev_line_prefix;
		$are_ok = 0;
	    }
	}
    }

    return $are_ok;
}

###############################################################################
# HUP the pqact program(s)
###############################################################################

sub ldmadmin_pqactHUP
{
    if ($os eq "SunOS" && $release =~ /^4/) {
	    open( IN, "ps -gawxl |" ) ||
		bad_exit("ps: Cannot open ps");
	    $default = 0 ;
    } elsif($os =~ /BSD/i) {
	    open( IN, "ps ajx |" ) ||
		bad_exit("ps: Cannot open ps");
	    $default = 1 ;

    } else {
	    open( IN, "ps -fu $ENV{'USER'} |" ) ||
		bad_exit("ps: Cannot open ps");
	    $default = 1 ;
    }
    # each platform has fields in different order, looking for PID
    $_ = <IN> ;
    s/^\s*([A-Z].*)/$1/ ;
    $index = -1 ;
    ( @F ) = split( /[ \t]+/, $_ ) ;
    for( $i = 0; $i <= $#F; $i++ ) {
	    next if( $F[ $i ] =~ /PPID/i ) ;
	    if( $F[ $i ] =~ /PID/i ) {
		    $index = $i ;
		    last ;
	    }
    }
    $index = $default if( $index == -1 ) ;

    @F = ( ) ;
    # Search through all processes, looking for "pqact".  Only processes that
    # are owned by the user will respond to the HUP signal.
    while( <IN> ) {
	    next unless( /pqact/ ) ;
	    s/^\s*([a-z0-9].*)/$1/ ;
	    ( @F ) = split( /[ \t]+/, $_ ) ;
	$pqactPid .= " $F[ $index ]" ;
    }
    close( IN ) ;

    if ($pqactPid eq "") {
	  errmsg("pqact: process not found, cannot HUP pqact");
    } else {
	  print "Check pqact HUP with command \"ldmadmin tail\"\n" ;
	  system( "kill -HUP $pqactPid" );
    }
}

###############################################################################
# Check the queue file for errors
###############################################################################

sub isProductQueueOk
{
    my $isOk = 0;
    my($status) = system("pqcheck -l- -q $q_path 2>/dev/null") >> 8;

    if( 0 == $status ) {
	print "The product-queue is OK.\n";
	$isOk = 1;
    }
    elsif (1 == $status) {
	errmsg(
	    "The self-consistency of the product-queue couldn't be " .
	    "determined.  See the logfile for details.");
    }
    elsif (2 == $status) {
	errmsg(
	    "The product-queue doesn't have a writer-counter.  Using " .
	    "\"pqcheck -F\" to create one...");
	system("pqcheck -F -q $q_path");
	if ($?) {
	    errmsg("Couldn't add writer-counter to product-queue.");
	}
	else {
	    $isOk = 1;
	}
    }
    elsif (3 == $status) {
	errmsg(
	    "The writer-counter of the product-queue isn't zero.  Either " .
	    "a process has the product-queue open for writing or the queue " .
	    "might be corrupt.  Terminate the process and recheck or use\n" .
	    "    pqcat -l- -s -q $q_path && pqcheck -F -q $q_path\n" .
	    "to validate the queue and set the writer-counter to zero.");
    }
    else {
	errmsg(
	    "The product-queue is corrupt.  Use\n" .
	    "    ldmadmin delqueue && ldmadmin mkqueue\n" .
	    "to remove and recreate it.");
    }
    return $isOk;
}

###############################################################################
# Remove product-information files that are older than the LDM pid-file.
###############################################################################

sub removeOldProdInfoFiles
{
     system("find .*.info -prune \! -newer $pid_file | xargs rm -f");
}

###############################################################################
# Check the system clock
###############################################################################

sub checkTime
{
    my $failure = 1;

    if (!$check_time) {
	if ($warn_if_check_time_disabled) {
	    errmsg("\n".
		"WARNING: The checking of the system clock is disabled.  ".
		"You might loose data if the clock is off.  To enable this ".
		"checking, execute the command \"regutil -u 1 ".
                "regpath{CHECK_TIME}\".");
	}
	$failure = 0;
    }
    else {
	if ($#time_servers < 0) {
	    errmsg("\nWARNING: No time-servers are specified by the registry ".
		"parameter \"regpath{NTPDATE_SERVERS}\". Consequently, the ".
		"system clock can't be checked and you might loose data if ".
		"it's off.");
	}
	else {
	    my @hosts = @time_servers;
	    while ($#hosts >= 0) {
		my $i = int(rand(scalar(@hosts)));
		my $timeServer = $hosts[$i];
		@hosts = (@hosts[0 .. ($i-1)], @hosts[($i+1) .. $#hosts]);
		if (!open(NTPDATE,
		    "$ntpdate -q -t $ntpdate_timeout $timeServer 2>&1 |")) {
		    errmsg("\n".
			"Couldn't execute the command \"$ntpdate\": $!.  ".
                        "Execute the command \"regutil -s path ".
                        "regpath{NTPDATE_COMMAND}\" to set the pathname of ".
                        "the ntpdate(1) utility to \"path\".");
		    last;
		}
		else {
		    my $offset;
		    while (<NTPDATE>) {
			if (/offset\s+([+-]?\d*\.\d*)/) {
			    $offset = $1;
			    last;
			}
		    }
		    close NTPDATE;
		    if (length($offset) == 0) {
			errmsg("\n".
			    "Couldn't get time from time-server at ".
			    "$timeServer using the ntpdate(1) utility, ".
			    "\"$ntpdate\".  ".
			    "If the utility is valid and this happens often, ".
			    "then remove $timeServer ".
			    "from registry parameter ".
                            "\"regpath{NTPDATE_SERVERS}\".");
		    }
		    else {
			if (abs($offset) > $check_time_limit) {
			    errmsg("\n".
				"The system clock is more than ".
				"$check_time_limit seconds off, which is ".
				"specified by registry parameter ".
				"\"regpath{CHECK_TIME_LIMIT}\".");
			}
			else {
			    $failure = 0;
			}
			last;
		    }
		}
	    }
	}
	if ($failure) {
	    errmsg("\n".
		"You should either fix the problem (recommended) or disable ".
		"time-checking by executing the command ".
                "\"regutil -u 0 regpath{CHECK_TIME}\" (not recommended).");
	}
    }
    return $failure;
}

###############################################################################
# Metrics:
###############################################################################

# Command for getting a UTC timestamp:
sub getTime
{
    chomp(my($time) = `date -u +%Y%m%d.%H%M%S`);
    return $time;
}
#
# Command for getting the running 1, 5, and 15 minute load averages:
sub getLoad
{
    chomp(my($output) = `uptime`);
    return (split(/,?\s+/, $output))[-3, -2, -1];
}
#
# Command for getting the number of connections to the LDM port (local, remote):
sub getPortCount
{
    my($localCount) = 0;
    my($remoteCount) = 0;
    open(FH, $netstat."|") or die "Can't fork() netstat(1): $!";
    while (<FH>) {
	if (/ESTABLISHED/) {
	    my(@fields) = split(/\s+/);
	    $localCount++ if ($fields[3] =~ /:$port$/);
	    $remoteCount++ if ($fields[4] =~ /:$port$/);
	}
    }
    (close FH || !$!) or die "Can't close(): status=$?";
    return ($localCount, $remoteCount);
}
#
# Command for getting product-queue metrics (age, #prods):
sub getPq
{
    my($age) = -1;
    my($prodCount) = -1;
    my($byteCount) = -1;
    open(FH, "pqmon -l- -q $q_path 2>&1 |") or die "Can't fork() pqmon(1): $!";
    while (<FH>) {
	my(@fields) = split(/\s+/);
	if ($#fields == 13) {
	    $age = $fields[13];
	    $prodCount = $fields[5];
	    $byteCount = $fields[8];
	}
    }
    (close FH || !$!) or die "Can't close(): status=$?";
    return ($age, $prodCount, $byteCount);
}
#
# Command for getting space-usage metrics:
#
sub getCpu
{
    my($userTime) = -1;
    my($sysTime) = -1;
    my($idleTime) = -1;
    my($waitTime) = -1;
    my($memUsed) = -1;
    my($memFree) = -1;
    my($swapUsed) = -1;
    my($swapFree) = -1;
    my($contextSwitches) = -1;
    my($haveMem) = 0;
    my($haveSwap) = 0;
    open(FH, $top."|") or die "Can't fork() top(1): $!";
    while (<FH>) {
	if (/^mem/i) {
	    s/k/e3/gi;
	    s/m/e6/gi;
	    s/g/e9/gi;
	    $memUsed = $1 if /([[:digit:]]+(e\d)?) used/i;
	    $memUsed = $1 if /([[:digit:]]+(e\d)?) phys/i;
	    $memFree = $1 if /([[:digit:]]+(e\d)?) free/i;
	    if ($memUsed < 0 && $memFree >= 0 && /([[:digit:]]+(e\d)?) real/i) {
		$memUsed = $1 - $memFree;
	    }
	    $haveMem = 1;
	    if (/swap/) {
		if (/([[:digit:]]+(e\d)?) (free swap|swap free)/i) {
		    $swapFree = $1;
		}
		elsif (/([[:digit:]]+(e\d)?) (total )?swap/i) {
		    $swapUsed = $1 - $swapFree;
		}
		if (/([[:digit:]]+(e\d)?) swap in use/i) {
		    $swapUsed = $1;
		}
		$haveSwap = 1;
	    }
	}
	elsif (/^swap/i) {
	    s/k/e3/gi;
	    s/m/e6/gi;
	    s/g/e9/gi;
	    /([[:digit:]]+(e\d)?) used/i;	$swapUsed = $1;
	    /([[:digit:]]+(e\d)?) free/i;	$swapFree = $1;
	    $haveSwap = 1;
	}
	last if ($haveMem && $haveSwap);
    }
    (close FH || !$!) or die "Can't close(): status=$?";

    my($csIndex) = -1;
    my($usIndex) = -1;
    my($syIndex) = -1;
    my($idIndex) = -1;
    my($waIndex) = -1;
    my($line) = "";
    open(FH, "vmstat 1 2|") or die "Can't fork() vmstat(1): $!";
    while (<FH>) {
	my(@fields) = split(/\s+/);
	for (my($i) = 0; $i <= $#fields; ++$i) {
	    if ($csIndex < 0 && $fields[$i] eq "cs") {
		$csIndex = $i;
	    }
	    elsif ($usIndex < 0 && $fields[$i] eq "us") {
		$usIndex = $i;
	    }
	    elsif ($syIndex < 0 && $fields[$i] eq "sy") {
		$syIndex = $i;
	    }
	    elsif ($idIndex < 0 && $fields[$i] eq "id") {
		$idIndex = $i;
	    }
	    elsif ($waIndex < 0 && $fields[$i] eq "wa") {
		$waIndex = $i;
	    }
	}
	$line = $_
    }
    (close FH || !$!) or die "Can't close(): status=$?";
    my(@fields) = split(/\s+/, $line);
    ($contextSwitches = $fields[$csIndex]) if $csIndex >= 0;
    ($sysTime = $fields[$syIndex]) if $syIndex >= 0;
    ($userTime = $fields[$usIndex]) if $usIndex >= 0;
    ($idleTime = $fields[$idIndex]) if $idIndex >= 0;
    ($waitTime = $fields[$waIndex]) if $waIndex >= 0;

    return ($userTime, $sysTime, $idleTime, $waitTime, 
	$memUsed, $memFree, $swapUsed, $swapFree, $contextSwitches);
}
#
# Command for printing metrics:
sub printMetrics
{
    print join(' ', getTime(), getLoad(), getPortCount(), getPq(), getCpu());
    print "\n";
    return $?;
}
#
# Command for plotting metrics:
sub plotMetrics
{
    return system("plotMetrics -b $begin -e $end $metrics_files");
}

###############################################################################
# Print an error-message
###############################################################################

sub errmsg
{
    $SIG{PIPE} = 'IGNORE';
    open(FH, "|fmt 1>&2")	or die "Can't fork() fmt(1): $!";
    print FH @_			or die "Can't write(): $!";
    close FH			or die "Can't close(): status=$?";
}
