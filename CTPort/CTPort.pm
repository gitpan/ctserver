package Telephony::CTPort;

# CTPort - part of ctserver client/server library for Computer Telephony 
# programming in Perl
#
# Copyright (C) 2001 David Rowe david@voicetronix.com.au
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

require 5.005_62;
use strict;
use warnings;
use Carp;
use IO::Socket;

require Exporter;

our @ISA = qw(Exporter);

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use Telephony::CTPort ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(
	
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
	
);
our $VERSION = '0.3';

# Preloaded methods go here.

# constructor - opens TCP/IP connection to server and makes sure we are
# on hook to start with
sub new($) {
    my $proto = shift;
    my $port = shift;
    my $class = ref($proto) || $proto;
    my $self = {};

    $self->{SERVER} = IO::Socket::INET->new(
				Proto => "tcp",
				PeerAddr => "localhost",
				PeerPort => $port,
				)
	or croak "cannot connect to server tcp port $port";

    $self->{EVENT}  = undef;
    $self->{DEF_EXT} = ".au";     # default audio file extension
    $self->{PATHS} = [];          # user supplied audio file paths
    $self->{INTER_DIGIT} = undef;

    bless($self, $class);
    $self->on_hook();

    return $self;
}

sub set_def_ext($) {
    my $self = shift;
    my $defext = shift;

    $self->{DEF_EXT} = $defext;
}

sub set_paths($) {
    my $self = shift;
    my $paths = shift;

    $self->{PATHS} = $paths;
}

sub event($) {
    my $self = shift;

    return $self->{EVENT};
}

sub off_hook() {
    my $self = shift;
    my $buf;
    my $server = $self->{SERVER};
    print $server "ctanswer\n";
    $buf = <$server>;
}

sub on_hook() {
    my $self = shift;
    my $buf;
    my $server = $self->{SERVER};
    print $server "cthangup\n";
    $buf = <$server>;
}

sub wait_for_ring() {
    my $self = shift;
    my $server = $self->{SERVER};
    my $caller_id;

    print $server "ctwaitforring\n";
    $caller_id = <$server>;

    return $caller_id;
}

sub wait_for_dial_tone() {
    my $self = shift;
    my $server = $self->{SERVER};
    my $buf;
    
    print $server "ctwaitfordial\n";
    $buf = <$server>;
}

sub play($) {
   my $self = shift;
   my $files_str = shift;
   my $file;

   unless ($files_str) {return;}
   my @files_array = split(/ /,$files_str);

   foreach $file (@files_array) {
       if (!$self->{EVENT}) {
	   $self->_ctplayonefile($file);
       }
   }
}

sub _ctplayonefile() {
   my $self = shift;
   my($file) = shift;
   my $server = $self->{SERVER};
   my $event;
   my $path;

   # append default extension if no extension on file name
   if ($self->{DEF_EXT}) {
       if ($file !~ /\./) {
	   $file = $file . $self->{DEF_EXT};
       }
   }
   
   # check user supplied paths
   if (defined($self->{PATHS})) {
       my @paths = $self->{PATHS};
       foreach $path (@paths) {

	   # find first path that contains the file

	   if (-e "$path/$file") {
	       print $server "ctplay\n$path/$file\n";
	       $event = <$server>;
	       $event =~ s/[^1-9ADCD#*]//g;
	       $self->{EVENT} = $event;
               return;			      
	   }
       }
   }

   # check default paths

   if (-e "$ENV{PWD}/$file") {
       # full path supplied by caller
       print $server "ctplay\n$ENV{PWD}/$file\n";
       $event = <$server>;
       $event =~ s/[^1-9ADCD#*]//g;
       $self->{EVENT} = $event;
       return;			      
   }

   if (-e "$ENV{PWD}/prompts/$file") {
       # prompts sub-dir of current dir
       print $server "ctplay\n$ENV{PWD}/prompts/$file\n";
       $event = <$server>;
       $event =~ s/[^1-9ADCD#*]//g;
       $self->{EVENT} = $event;
       return;			      
   }

   if (-e "/var/ctserver/USEngM/$file") {
       # USEngM prompts dir
       print $server "ctplay\n/var/ctserver/USEngM/$file\n";
       $event = <$server>;
       $event =~ s/[^1-9ADCD#*]//g;
       $self->{EVENT} = $event;
       return;			      
   }

   carp "play: File $file not found!\n";
} 

sub record($$$) {
   my $self = shift;
   my $file = shift;
   my $timeout = shift;
   my $term_digits = shift;
   my $server = $self->{SERVER};
   my $event;
   my @unpacked_file = split(//, $file);

   unless ($unpacked_file[0] eq "/") {
       # if not full path, record in current dir
       $file = "$ENV{PWD}/$file";
   }
   print $server "ctrecord\n$file\n$timeout\n$term_digits\n";
   $event = <$server>;
   $event =~ s/[^1-9ADCD#*]//g;
   $self->{EVENT} = $event;
} 

sub ctsleep($) {
    my $self = shift;
    my $secs = shift;
    my $server = $self->{SERVER};
    my $event;

    if (!$self->{EVENT}) {
	print $server "ctsleep\n$secs\n";
	$event = <$server>;
	$event =~ s/[^1-9ADCD#*]//g;
        $self->{EVENT} = $event;
    }
}

sub clear() {
    my $self = shift;
    my $server = $self->{SERVER};
    my $tmp;

    print $server "ctclear\n";
    $tmp = <$server>;
    undef $self->{EVENT};
}

sub collect($$) {
    my $self = shift;
    my $maxdigits = shift;
    my $maxseconds = shift;
    my $maxinter;
    my $server = $self->{SERVER};
    my $digits;

    $maxinter = $self->{INTER_DIGIT} || $maxseconds;
    undef $self->{EVENT};

    print $server "ctcollect\n$maxdigits\n$maxseconds\n$maxinter\n";
    $digits = <$server>; 
    $digits =~ s/[^1-9ADCD#*]//g;
    return $digits;		  
}

sub dial($) {
    my $self = shift;
    my($dial_str) = shift;
    my $server = $self->{SERVER};
    my($tmp);

    print $server "ctdial\n$dial_str\n";
    $tmp = <$server>; 
}

sub number($) {
    my $self = shift;
    my $num = shift;
    my $server = $self->{SERVER};
    my $tens;
    my $hundreds;
    my @files;
    my $all;
    
    unless ($num) {return undef};

    if ($num == 0) { push(@files, $num); }

    $hundreds = int($num/100) * 100;
    $num = $num - $hundreds;
    if ($hundreds != 0) { push(@files, $hundreds); }

    $tens = int($num/10) * 10;

    if ($num > 20) { 
	$num = $num - $tens;
	if ($tens != 0) { push(@files, $tens); }
    }

    if ($num != 0) { push(@files, $num); }

    $all = "@files";
    return $all;
}

sub get_inter_digit_time_out() {
    my $self = shift;

    return $self->{INTER_DIGIT};
}
    
sub set_inter_digit_time_out($) {
    my $self = shift;
    my $inter = shift;

    $self->{INTER_DIGIT} = $inter;
}
    
1;

__END__

# Documentation for module

=head1 NAME

Telephony::CTPort - Computer Telephony programming in Perl

=head1 SYNOPSIS

 use Telephony::CTPort;

 $ctport = new Telephony::CTPort(1200); # first port of CT card
 $ctport->off_hook;
 $ctport->play("beep");                 
 $ctport->record("prompt.wav",5,"");    # record for 5 seconds
 $ctport->play("prompt.wav");           # play back
 $ctport->on_hook;

=head1 DESCRIPTION

This module implements an Object-Oriented interface to control Computer 
Telephony (CT) card ports using Perl.  It is part of a client/server
library for rapid CT application development using Perl.

=head1 AUTHOR

David Rowe, david@voicetronix.com.au

=head1 CONSTRUCTOR

new Telephony::CTPort(SERVER_PORT);

Connects Perl client to the "ctserver" server via TCP/IP port SERVER_PORT,
where SERVER_PORT=1200, 1201,..... etc for the first, second,..... etc
CT ports.

=head1 METHODS

event() - returns the most recent event, or undef if no events pending.

off_hook() - takes port off hook, just like picking up the phone.

on_hook() - places the port on hook, just like hanging up.

wait_for_ring() - blocks until port detects a ring, then returns.  The caller
ID (if present) will be returned.

wait_for_dial_tone() - blocks until dial tone detected on port, then returns.

play($files) - plays audio files, playing stops immediately if a DTMF key is 
pressed.  The DTMF key pressed can be read using the event() member function.
If $ctport->event() is already defined it returns immediately.  Any digits
pressed while playing will be added to the digit buffer.

Filename extensions:

=over 4

=item *

default is .au, can be redefined by calling set_def_ext()

=item *

override default by providing extension, e.g. $ctport->play("hello.wav");

=back

Searches for file in:

=over 4

=item *

paths defined by set_path() method

=item *

current dir

=item *

"prompts" sub dir (relative to current dir)

=item *

full path supplied by caller

=item *

/var/ctserver/UsMEng

=back

You can play multiple files, e.g. 

$ctport->play("Hello World"); 

(assumes you have Hello.au and World.au files available)

You can "speak" a limited vocab, e.g. 

$ctport->play("1 2 3"); 

(see /var/ctserver/UsMEng directory for the list of included files that define
the vocab)

record($file_name, $time_out, $term_keys) - records $file_name for 
$time_out seconds or until any of the digits in $term_keys are pressed.
The path of $file_name is considered absolute if there is a leading /, 
otherwise it is relative to the current directory.

ctsleep($seconds) - blocks for $seconds, unless a DTMF key is pressed in which
case it returns immediately.  If $ctport->event() is already defined it 
returns immediately without sleeping.

clear() - clears any pending events, and clears the DTMF digit buffer.

collect($max_digits, $max_seconds) - returns up to $max_digits by waiting up 
to $max_seconds.  Will return as soon as either $max_digits have been collected
or $max_seconds have elapsed.  On return, the event() method will return
undefined.  

DTMF digits pressed at any time are collected in the digit buffer.  The digit
buffer is cleared by the clear() method.  Thus it is possible for this function
to return immediately if there are already $max_digits in the digit buffer.

dial($number) - Dials a DTMF string.  Valid characters are 1234567890#*,&

=over 4

=item *

, gives a 1 second pause, e.g. $ctport->dial(",,1234) will wait 2 seconds, 
then dial extension 1234.

=item *

& generates a hook flash (used for transfers on many PBXs) e.g. :

$ctport->dial("&,1234) will send a flash, wait one second, then dial 1234. 

=back

number() - returns a string of audio files that enable numbers to be "spoken"

e.g. number() will convert 121 into "one hundred twenty one" 

e.g. ctplay("youhave " . $ctnumber($num_mails) . " mails");

(assumes files youhave.au, mails.au, and variable $num_mails exist)

set_path() - used to set the search path for audio files supplied to play()

get_inter_digit_time_out() - returns the optional inter-digit time out used
with collect().

set_inter_digit_time_out($time_out) - sets the optional inter-digit time out 
used with collect().
