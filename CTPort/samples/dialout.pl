#!/usr/bin/perl -w
# dialout.pl
# David Rowe 19/10/01
#
# Demonstrates dialing out.
 
# ctserver - client/server library for Computer Telephony programming in Perl
# Copyright (C) 2001 David Rowe david@voicetronix.com.au
# see COPYING.TXT

use Telephony::CTPort;

$ctport = new Telephony::CTPort(1200); 
sleep(1);
$ctport->off_hook;            # take off hook
$ctport->wait_for_dial_tone;  # block until we get dial tone   
$ctport->dial("11");          # dial extension 11
sleep(10);                    # ring for 10 seconds
$ctport->on_hook;







