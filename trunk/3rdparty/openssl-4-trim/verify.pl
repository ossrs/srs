#!/usr/bin/perl
# Cross-check one inventory: every file of the tree that the compiler's dependency files (*.d, from -MMD)
# name, and every build.info that configdata.pm lists, must be in read.txt.
# Usage: verify.pl <inventory-dir>
use strict; use warnings;
my $dir = shift or die "usage: verify.pl <inventory-dir>\n";
my $tree = "$dir/tree";
my %all  = map { chomp; $_ => 1 } do { open my $f, '<', "$dir/all.txt" or die; <$f> };
my %read = map { chomp; $_ => 1 } do { open my $f, '<', "$dir/read.txt" or die; <$f> };
my (%deps, $nd);
open my $find, '-|', 'find', $tree, '-name', '*.d', '-type', 'f' or die;
while (my $d = <$find>) {
    chomp $d; $nd++;
    open my $f, '<', $d or die "$d: $!";
    local $/; my $s = <$f>; $s =~ s/\\\n/ /g;
    for my $tok (split /\s+/, $s) {
        next if $tok eq '' || $tok =~ /:$/;
        $tok =~ s|^\Q$tree\E/||; $tok =~ s|^\./||;
        1 while $tok =~ s|[^/]+/\.\./||;
        $deps{$tok} = 1 if $all{$tok};
    }
}
my @miss = grep { !$read{$_} } sort keys %deps;
my $cd = do { open my $f, '<', "$dir/configdata.pm" or die; local $/; <$f> };
my @bi;
if ($cd =~ /"build_infos"\s*=>\s*\[(.*?)\]/s) { @bi = $1 =~ /"([^"]+)"/g; }
for (@bi) { s|^\Q$tree\E/||; s|^\./||; }
my @bimiss = grep { $all{$_} && !$read{$_} } @bi;
printf "%s: %d .d files, %d tree files in deps, %d missing from read.txt; %d build.info listed, %d missing\n",
    $dir, $nd // 0, scalar(keys %deps), scalar(@miss), scalar(@bi), scalar(@bimiss);
print "  missing dep: $_\n" for @miss[0 .. ($#miss < 9 ? $#miss : 9)];
print "  missing build.info: $_\n" for @bimiss;
exit((@miss || @bimiss) ? 1 : 0);
