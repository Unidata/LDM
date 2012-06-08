/**
 * @mainpage NOAAPORT Data Ingestion
 *
 * @section contents Table of Contents
 * - \ref introduction
 * - \ref preinstallation
 * - \ref configuration
 * - \ref logging
 * - \ref performance
 *
 * <hr>
 *
 * @section introduction Introduction
 * This system captures NOAAPORT broadcast UDP packets from a DVB-S receiver,
 * creates LDM data-products from the data, and inserts those products into an
 * LDM product-queue. The main programs of this system are 1) the \link
 * noaaportIngester.c \c noaaportIngester \endlink program; 2) the deprecated
 * \link dvbs_multicast.c \c dvbs_multicast \endlink program; 3) a
 * shared-memory FIFO ; and 4) the deprecated \link readnoaaport.c \c
 * readnoaaport \endlink program.  The shared-memory FIFO is used to buffer the
 * flow of data from the \link dvbs_multicast.c \c dvbs_multicast \endlink
 * program to the \link readnoaaport.c \c readnoaaport \endlink program.
 *
 * <hr>
 *
 * @section preinstallation Preinstallation
 * Operating systems based on Redhat Linux (including Fedora) must have two
 * kernel parameters modified from their default values. The parameter
 * \c net.ipv4.ipfrag_max_dist must be modified from its default value of
 * of \c 64 in order for all broadcast UDP packets to be correctly received
 * without any gaps.
 * Its value should be \c 4096 or higher or the value should be
 * \c 0 in order to disable the check of the sequencing of IP fragments.
 * The value of parameter \c net.ipv4.conf.default.rp_filter should be
 * \c 2 in order to obtain correct support for a multi-homed system.
 * For example, as \c root, execute the commands
 * \verbatim
 sysctl -w net.ipv4.ipfrag_max_dist=4096
 sysctl -w net.ipv4.conf.default.rp_filter=2
 sysctl -p
\endverbatim
 *
 * <hr>
 *
 * @section configuration Configuration
 * Edit the LDM configuration file, \c ~/etc/ldmd.conf, and add the entries
 * needed to read and process the DVB-S stream(s).  Here's one possibility
 * using the \link noaaportIngester.c \c noaaportIngester \endlink program:
 *
 * \verbatim
 # DVB-S ingest
 EXEC    "noaaportIngester -m 224.0.1.1"
 EXEC    "noaaportIngester -m 224.0.1.2"
 EXEC    "noaaportIngester -m 224.0.1.3"
 EXEC    "noaaportIngester -m 224.0.1.4"
\endverbatim
 *
 * Alternatively, the deprecated \link dvbs_multicast.c \c dvbs_multicast
 * \endlink and \link readnoaaport.c \c
 * readnoaaport \endlink programs could be used:
 *
 * \verbatim
 # DVB-S broadcast UDP listening and shared-memory writing processes
 EXEC    "dvbs_multicast -m 224.0.1.1"
 EXEC    "dvbs_multicast -m 224.0.1.2"
 EXEC    "dvbs_multicast -m 224.0.1.3"
 EXEC    "dvbs_multicast -m 224.0.1.4"

 # Shared-memory reading and data-product creation & insertion processes
 EXEC    "readnoaaport -m 224.0.1.1"
 EXEC    "readnoaaport -m 224.0.1.2"
 EXEC    "readnoaaport -m 224.0.1.3"
 EXEC    "readnoaaport -m 224.0.1.4"
\endverbatim
 *
 * These \c ldmd.conf actions create a \link dvbs_multicast.c \c
 * dvbs_multicast \endlink process for each available PID from the UDP
 * multicast stream. Each such process places the received packets into a
 * shared-memory buffer for reading by a corresponding \link readnoaaport.c \c
 * readnoaaport \endlink process.  There must be one \link readnoaaport.c \c
 * readnoaaport \endlink process for each \link dvbs_multicast.c \c
 * dvbs_multicast \endlink process.
 *
 * Restart the LDM system, if appropriate.
 *
 * <hr>
 *
 * @section logging Logging
 * The programs in this system use the LDM 
 * logging facility.  The default log file for the LDM is, typically,
 * \c ~/logs/ldmd.log.  We've found it useful to override the
 * default logging and have each instance of
 * \link noaaportIngester.c \c noaaportIngester \endlink or
 * \link readnoaaport.c \c readnoaaport \endlink
 * write a notice of every processed product to its own log file.
 * We do this via the
 * \c -u \e X option, where \e X refers to
 * the <tt>local</tt><em>X</em> logging facility:
 *
 * \verbatim
 EXEC    "noaaportIngester -m 224.0.1.1 -n -u 3"
\endverbatim
 *
 * or
 *
 * \verbatim
 EXEC    "readnoaaport -m 224.0.1.1 -n -u 3"
\endverbatim
 *
 * If you are not interested in logging to seperate files, simply omit
 * the \c -u \e X option.
 *
 * Since LDM logging uses \c syslogd(8), one must add additional
 * configuration lines to \c /etc/syslog.conf. 
 * The standard additions to \c /etc/syslog.conf for the LDM are:
 *
 * - Inclusion of \c local0.none in the default system logginf file
 *
 * - Addition of a line that says where to write log messages for log
 *   facility '0'.
 *
 * Here is an example of how this looks on our Fedora Linux system:
 * \verbatim
 *.info;mail.none;news.none;authpriv.none;cron.none;local0.none       /var/log/messages

 # LDM logging
 local0.debug                                                         /home/ldm/logs/ldmd.log
\endverbatim
 *
 * To setup \c syslogd(8) to log to a different file for each ingest script,
 * one has to add more entries to \c /etc/syslog.conf.  Here is an example
 * of how we have \c /etc/syslog.conf setup on our Fedora system
 * that is running the LDM and ingesting the \c nwstg2 and \c oconus streams
 * in the current DVB-S NOAAPORT stream:
 * \verbatim
 *.info;mail.none;news.none;authpriv.none;cron.none;local0.none;local3.none;local4.none;local5.none;local6.none  /var/log/messages

 # LDM logging
 local0.debug                         /home/ldm/logs/ldmd.log

 #
 # LDM NOAAport ingest logging
 #
 local3.debug                         /data/tmp/nwstg.log
 local4.debug                         /data/tmp/goes.log
 local5.debug                         /data/tmp/nwstg2.log
 local6.debug                         /data/tmp/oconus.log
\endverbatim
 *
 * \note
 *
 * - Given the limited number of log facilities, one might be forced
 * to combine logging for more than one DVB-S ingest process.
 *
 * - Modifications to \c /etc/syslog.conf must be done as \c root.
 *
 * - While some operating systems do not care what the white space is
 * in \c /etc/syslog.conf, some do.  It is always safest to make sure that
 * white spaces in non-comment lines are tabs.  (Important!).
 *
 * The log files for our NOAAPORT ingest processes can become very large
 * in a hurry.  Because of this, it is best to rotate the files
 * once-per-day.  Here is the \c crontab(1) entry we currently use to do the
 * file rotation:
 * \verbatim
 # rotate NOAAport ingest logs
 0 0 * * * bin/nplog_rotate 4
\endverbatim
 *
 * Here, 4 is the number of days of all DVB-S readnoaaport invocation
 * log files to keep online.
 *
 * <hr>
 *
 * @section performance Performance considerations if you use dvbs_multicast
 * \link dvbs_multicast.c \c dvbs_multicast \endlink
 * is the program which reads the UDP packets from
 * a PID on the multicast. The reading of the UDP stream must be 
 * able to keep up with the stream since there is no mechanism for
 * UDP packets to be retransmitted. We have found that the process which reads
 * these UDP packets can see gaps in the data stream if the process
 * is swapped out by the operating system. To alleviate this possibilty
 * we recommend the following:
 *
 *    - Ensure that the program
 *      \link dvbs_multicast.c \c dvbs_multicast \endlink
 *      is owned by \c root and setuid. This will be done by the installation
 *      process \e if the superuser's password was given to the \c
 *      configure script or the LDM user has \c sudo(1) privileges and
 *      the LDM user's password was given to the \c configure script.
 *      Otherwise, the command <tt>make root-actions</tt> must be executed by
 *      the superuser (i.e., \c root) after the system has been
 *      installed.
 *      This will ensure that the program stays resident in memory and will
 *      not be paged-out.
 *
 *    - Do \e not have each
 *      \link readnoaaport.c \c readnoaaport \endlink
 *      process log every data-product that it processes.
 *
 *    - Consider an LDM product-queue small enough to fit into available
 *      memory to avoiding the need for the system to do lots of paging.
 */
