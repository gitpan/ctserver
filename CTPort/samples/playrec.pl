#!/usr/bin/perl -w
# playrec.pl
# David Rowe 19/10/01
#
# Implementation of the classic Bayonne "playrec":
# 1. Prompts caller to record or play a message
# 2. Asks caller for 3 digit file name, then plays/record that file.
# 3. Dont forget to start ctserver before starting!

# ctserver - client/server library for Computer Telephony programming in Perl
# Copyright (C) 2001 David Rowe david@voicetronix.com.au
# see COPYING.TXT

use Telephony::CTPort;

sub playrec();                         # prototype to stop warning

$ctport = new Telephony::CTPort(1200); # first port of CT card
 
while(1) {
    $ctport->wait_for_ring();          # block until ring
    $ctport->off_hook;                 # take CT card port off hook
    playrec();                       
    $ctport->on_hook;                  # all finished, so hang up
}

sub playrec() {
    while (1) {
	$ctport->clear;                # clears any events and digit buffer
        $ctport->play("playrec.ul");   # "Press 1 to Play, or 2 to record"
	$ctport->ctsleep(10);          # Wait ten seconds unless key pressed
	unless ($ctport->event) {      # if no events then finished
	    return;
	}

	if ($ctport->event eq "1") {         # DTMF digit 1 pressed
	    $ctport->clear;                  # clear events and digit buffer
	    $ctport->play("playrec1.ul");    # "Enter the 3 digit number..."
	    $digits = $ctport->collect(3,3); # wait for three digits or timeout
	    $playfile = $digits . ".wav";
	    $ctport->play($playfile);        
	}

	elsif ($ctport->event eq "2") {        # DTMF digit 2 pressed
	    $ctport->clear;
	    $ctport->play("playrec2.ul");      # "Enter the three digit number"
	    $digits = $ctport->collect(3,3);  
	    $recfile = $digits . ".wav";
	    $ctport->record($recfile,6,"*#");  # record file, up to 6
                                               # seconds, or terminate
                                               # by pressing # or *
	}

	elsif ($ctport->event eq "#") {        # causes hangup
	    return;
	}

	else {
	    $ctport->play("playrec0.ul");      # "You have pressed an invaild"
	}
    }
}





