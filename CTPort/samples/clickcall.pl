#!/usr/bin/perl -w
# clickcall.pl
# David Rowe 19/10/01
#
# Demonstrates a simple web-based click to call application:
# 1. Place this file in the cgi-bin dir of your web server.
# 2. hit http://your-server/cgi-bin/clickcall.pl
# 3. Enter the numbers to call hit "Call"
# 4. When phone pick up, press 1 on phone to end call.
 
# ctserver - client/server library for Computer Telephony programming in Perl
# Copyright (C) 2001 David Rowe david@voicetronix.com.au
# see COPYING.TXT

use Telephony::CTPort;
use CGI qw(:standard);

my $number = param("number");

print header, start_html("Click Call Demo"), h1("Click Call Demo");

if ($number) {
    # CT card will now dial number

    $ctport = new Telephony::CTPort(1200); 
    sleep(1);
    $ctport->off_hook;            # take off hook
    $ctport->wait_for_dial_tone;  # block until we get dial tone   
    $ctport->dial($number);       # dial number

    $ctport->clear();
    $ctport->collect(1,30);       # wait 30 seconds for 1 digit
    $ctport->on_hook;

    print hr, "Call Completed!", hr;

} else {
    # display the form
    print hr, start_form;
    print p("Phone Number to Call: ", textfield("number"));
    print p(submit("Call"));
    print end_form, hr;
    print br, "When the phone rings pick up and press 1 on phone to end call";
    print br;
}

print end_html;
