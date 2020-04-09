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
 * This system captures NOAAPORT multicast UDP packets from a DVB-S receiver,
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
 * Operating systems based on Redhat Linux (e.g., RHEL, Fedora, CentOS) must
 * have two kernel parameters modified from their default values. The parameter
 * \c net.ipv4.ipfrag_max_dist controls the process by which UDP fragments are
 * reassembled into a UDP packet. Because NOAAPORT UDP packets are not
 * fragmented, this re-assembly process should be disabled by setting this
 * parameter to \c 0 in order to prevent spurious gaps from occurring in the UDP
 * packet stream.
 * Also, the value of parameter \c net.ipv4.conf.default.rp_filter should be
 * \c 2 in order to obtain correct support for a multi-homed system.
 * As \c root, execute the commands
 * \verbatim
 sysctl -w net.ipv4.ipfrag_max_dist=0
 sysctl -w net.ipv4.conf.default.rp_filter=2
 sysctl -p
\endverbatim
 * If, after making these changes and (re)starting the LDM, the LDM doesn't see
 * any incoming data, then you should try rebooting your system and (optionally)
 * using the \c tcpdump(8) utility to verify that UDP packets are arriving at
 * the relevant interface.
 *
 * <hr>
 *
 * @section configuration Configuration
 * Edit the LDM configuration file, \c ~/etc/ldmd.conf, and add the entries
 * needed to read and process the DVB-S stream(s).  Here's one possibility
 * using the \link noaaportIngester.c \c noaaportIngester \endlink program and
 * the \c keep_running script to ensure that the program is restarted if it
 * crashes (due to a malformed GRIB2 message, for example).
 *
 * \verbatim
 # NOAAPort DVB-S ingest
 EXEC "keep_running noaaportIngester -m 224.0.1.1  -l var/logs/nwstg.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.2  -l var/logs/goes.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.3  -l var/logs/nwstg2.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.4  -l var/logs/oconus.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.5  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.6  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.7  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.8  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.9  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.10 -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.1.1.1  -l var/logs/wxwire.log"
\endverbatim
 * Note that the \c "-l" option is used to separate log messages.
 *
 * Alternatively, the deprecated \link dvbs_multicast.c \c dvbs_multicast
 * \endlink and \link readnoaaport.c \c
 * readnoaaport \endlink programs could be used:
 *
 * \verbatim
 # DVB-S multicast UDP listening and shared-memory writing processes
 EXEC    "keep_running dvbs_multicast -m 224.0.1.1"
 EXEC    "keep_running dvbs_multicast -m 224.0.1.2"
 EXEC    "keep_running dvbs_multicast -m 224.0.1.3"
 EXEC    "keep_running dvbs_multicast -m 224.0.1.4"
 ...

 # Shared-memory reading and data-product creation & insertion processes
 EXEC    "keep_running readnoaaport -m 224.0.1.1"
 EXEC    "keep_running readnoaaport -m 224.0.1.2"
 EXEC    "keep_running readnoaaport -m 224.0.1.3"
 EXEC    "keep_running readnoaaport -m 224.0.1.4"
 ...
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
 * The programs that ingest NOAAPort use the same logging mechanism as the
 * rest of the LDM package. This means that the ingest programs will either use
 * the default LDM logging mechanism or one based on \c syslogd(8) -- depending
 * on how the package was configured. See the section on logging in the LDM
 * reference documentation for details.
 *
 * @subsection default Using default LDM logging
 * The destination for log messages depends on the \c -l \e pathname option. If
 * no such option is specified, then the ingest processes will log
 * to the default LDM log file (typically \c ~/var/logs/ldmd.log). If, however,
 * that option is specified, then the ingest processes will log to the given
 * destination.
 *
 * @subsection syslogd Using syslogd(8) logging
 * We've found it useful to override the
 * default logging destination and have each instance of
 * \link noaaportIngester.c \c noaaportIngester \endlink or
 * \link readnoaaport.c \c readnoaaport \endlink
 * write a notice of every processed product to its own log file.
 * We do this via the
 * \c -u \e X option, where \e X refers to
 * the <tt>local</tt><em>X</em> logging facility:
 *
 * \verbatim
 EXEC    "keep_running noaaportIngester -m 224.0.1.1 -n -u 3"
\endverbatim
 *
 * or
 *
 * \verbatim
 EXEC    "keep_running readnoaaport -m 224.0.1.1 -n -u 3"
\endverbatim
 *
 * If you are not interested in logging to seperate files, simply omit
 * the \c -u \e X option and skip the rest of this section.
 *
 * If you do choose this approach, then you must add additional
 * configuration lines to \c syslogd(8)'s configuration-file,
 * \c /etc/syslog.conf.
 * The standard additions to \c /etc/syslog.conf for the LDM are:
 *
 * - Inclusion of \c local0.none in the default system logging file
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
 * To setup \c syslogd(8) to log to a different file for each ingester,
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
