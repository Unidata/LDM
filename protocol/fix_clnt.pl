#!/usr/bin/perl

my($nullResultsProc);
my($zeroTimeout);

while( <STDIN> ) {

    s/".*ldm\.h"/"ldm.h"/;

    if (/static[ 	]+struct[ 	]+timeval[ 	]+TIMEOUT/) {
	s/25/60/;  # NOTE: INT_MAX causes EINVAL in clnt_call()
	print $_;
	print "static const struct timeval ZERO_TIMEOUT = { 0, 0 };";
	print "\n";
	next;
    }

    if (/notification_6/ || /hereis_6/ || /blkdata_6/) {
        # Uncomment-out the following line to get "batched" RPC instead of
        # asynchronous "message-passing" RPC.
#	$nullResultsProc = 1;
	$zeroTimeout = 1;
    }

    if (/request_product_7/ ||
            /request_backlog_7/ ||
            /deliver_missed_product_7/ ||
            /no_such_product_7/ ||
            /deliver_backlog_product_7/ ||
            /end_backlog_7/ ||
            /test_connection_7/) {
	$zeroTimeout = 1;
    }

    if ($nullResultsProc) {
	s/xdr_void/NULL/;
    }

    if ($zeroTimeout) {
	s/TIMEOUT/ZERO_TIMEOUT/
    }

    print $_;

    if (/^}/) {
	$nullResultsProc = 0;
	$zeroTimeout = 0;
    }
}

print "\n";
print "void*\n";
print "nullproc_6(void *argp, CLIENT *clnt)\n";
print "{\n";
print "        static char clnt_res;\n";
print "        if (clnt_call(clnt, NULLPROC,\n";
print "                (xdrproc_t) xdr_void, (void*) argp,\n";
print "                (xdrproc_t) xdr_void, (void*) &clnt_res,\n";
print "                TIMEOUT) != RPC_SUCCESS) {\n";
print "            return NULL;\n";
print "        }\n";
print "        return ((void *)&clnt_res);\n";
print "}\n";
print "\n";
print "\n";
print "enum clnt_stat clnt_stat(CLIENT *clnt)\n";
print "{\n";
print "    struct rpc_err rpcErr;\n";
print "\n";
print "    clnt_geterr(clnt, &rpcErr);\n";
print "\n";
print "    return rpcErr.re_status;\n";
print "}\n";
