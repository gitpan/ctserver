use Telephony::CTPort;

my $caller_id;

$ctport = new Telephony::CTPort(1200); # first port of CT card
$caller_id = $ctport->wait_for_ring();
print "cid: $caller_id\n";
