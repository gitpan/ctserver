use Telephony::CTPort;

$ctport = new Telephony::CTPort(1200); # first port of CT card

$ctport->clear;
$ctport->set_inter_digit_time_out(2);
print "inter: " . $ctport->get_inter_digit_time_out . "\n";

$ctport->off_hook;

my $digits = $ctport->collect(2, 10);
print "digits: $digits\n";
$ctport->play($ctport->number($digits));

$ctport->on_hook;
