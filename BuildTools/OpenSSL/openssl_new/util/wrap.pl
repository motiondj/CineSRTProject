#! /usr/bin/env perl

use strict;
use warnings;

use File::Basename;
use File::Spec::Functions;

BEGIN {
    # This method corresponds exactly to 'use OpenSSL::Util',
    # but allows us to use a platform specific file spec.
    require 'C:/CineSRTProject/BuildTools/OpenSSL/openssl_new/util/perl/OpenSSL/Util.pm';
    OpenSSL::Util->import();
}

my $there = canonpath(catdir(dirname($0), updir()));
my $std_engines = catdir($there, 'engines');
my $std_providers = catdir($there, 'providers');
my $std_openssl_conf = catdir($there, 'apps/openssl.cnf');
my $unix_shlib_wrap = catfile($there, 'util/shlib_wrap.sh');

if ($ARGV[0] eq '-fips') {
    $std_openssl_conf = 'C:/CineSRTProject/BuildTools/OpenSSL/openssl_new/test/fips-and-base.cnf';
    shift;

    my $std_openssl_conf_include = catdir($there, 'providers');
    $ENV{OPENSSL_CONF_INCLUDE} = $std_openssl_conf_include
        if ($ENV{OPENSSL_CONF_INCLUDE} // '') eq ''
            && -d $std_openssl_conf_include;
}

$ENV{OPENSSL_ENGINES} = $std_engines
    if ($ENV{OPENSSL_ENGINES} // '') eq '' && -d $std_engines;
$ENV{OPENSSL_MODULES} = $std_providers
    if ($ENV{OPENSSL_MODULES} // '') eq '' && -d $std_providers;
$ENV{OPENSSL_CONF} = $std_openssl_conf
    if ($ENV{OPENSSL_CONF} // '') eq '' && -f $std_openssl_conf;

my $use_system = 0;
my @cmd;

if ($^O eq 'VMS') {
    # VMS needs the command to be appropriately quotified
    @cmd = fixup_cmd(@ARGV);
} elsif (-x $unix_shlib_wrap) {
    @cmd = ( $unix_shlib_wrap, @ARGV );
} else {
    # Hope for the best
    @cmd = ( @ARGV );
}

# The exec() statement on MSWin32 doesn't seem to give back the exit code
# from the call, so we resort to using system() instead.
my $waitcode = system @cmd;

# According to documentation, -1 means that system() couldn't run the command,
# otherwise, the value is similar to the Unix wait() status value
# (exitcode << 8 | signalcode)
die "wrap.pl: Failed to execute '", join(' ', @cmd), "': $!\n"
    if $waitcode == -1;

# When the subprocess aborted on a signal, we simply raise the same signal.
kill(($? & 255) => $$) if ($? & 255) != 0;

# If that didn't stop this script, mimic what Unix shells do, by
# converting the signal code to an exit code by setting the high bit.
# This only happens on Unix flavored operating systems, the others don't
# have this sort of signaling to date, and simply leave the low byte zero.
exit(($? & 255) | 128) if ($? & 255) != 0;

# When not a signal, just shift down the subprocess exit code and use that.
my $exitcode = $? >> 8;

# For VMS, perl recommendations is to emulate what the C library exit() does
# for all non-zero exit codes, except we set the error severity rather than
# success.
# Ref: https://perldoc.perl.org/perlport#exit
#      https://perldoc.perl.org/perlvms#$?
if ($^O eq 'VMS' && $exitcode != 0) {
    $exitcode =
        0x35a000                # C facility code
        + ($exitcode * 8)       # shift up to make space for the 3 severity bits
        + 2                     # Severity: E(rror)
        + 0x10000000;           # bit 28 set => the shell stays silent
}
exit($exitcode);
