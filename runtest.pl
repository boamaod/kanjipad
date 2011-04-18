#!/usr/bin/perl -w

use IO::File;
use IPC::Open2 qw(open2);

use strict;

my $in = new IO::File;
my $out = new IO::File;

open2( $in, $out, "./kpengine") || die "Cannot run recognition engine";

$out->autoflush();

if (!@ARGV) {
    push @ARGV, "samples.dat";
}

my $unknown = 0;
my @scores;

$/ = "";
while (<>) {
    my ($jis, $strokes) = /(\w{4})\s[^\n]*\n(.*)/s;
    defined $jis or warn "Bad stroke entry:\n$_\n", next;
    next if ($jis eq "0000");

    print $out $strokes;
    $/ = "\n";
    my @result = split(' ',substr(scalar <$in>, 1));
    $/ = "";

    my $score = 0;
    for (@result) {
	last if $_ eq $jis;
	$score++;
    }
    
    if ($score == @result) {
	$unknown++;
    } else {
	$scores[$score]++;
    }
}

print "Not found: $unknown\n";
print "Found, at rank:\n";

for (0..$#scores) {
    print $_+1," $scores[$_]\n";
}
