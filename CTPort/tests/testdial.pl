use Telephony::CTPort;

my $caller_id;

$ctport = new Telephony::CTPort(1200); # first port of CT card
$ctport->off_hook;
$ctport->wait_for_dial_tone();
print "dial tone detected!\n";
$ctport->on_hook;
