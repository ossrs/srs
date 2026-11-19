#!/usr/bin/perl
# The union of the targets' read.txt lists, plus keep-extra.txt, is the keep list. Prints the files read per
# target, the tree and keep totals, and the files and lines left out by directory; writes keep.txt and
# drop.txt into <out-dir>.
# Usage: report.pl <tree> <out-dir> <inventory-dir>...
use strict; use warnings;
my ($tree, $out, @inv) = @ARGV;
die "usage: report.pl <tree> <out-dir> <inventory-dir>...\n" unless @inv;
my @all = do { open my $f, '-|', 'find', $tree, '-type', 'f' or die; map { chomp; s|^\Q$tree\E/||; $_ } <$f> };
# The files no build reads but that must stay: the license, and the sources Configure only checks exist.
my %keep;
{
    (my $extra = __FILE__) =~ s|[^/]*$|keep-extra.txt|;
    open my $f, "<", $extra or die "$extra: $!";
    for (<$f>) { chomp; $keep{$_} = 1 if $_ ne "" && !/^#/; }
}
for my $d (@inv) {
    open my $f, '<', "$d/read.txt" or die "$d/read.txt: $!";
    my @r = map { chomp; $_ } <$f>;
    $keep{$_}++ for @r;
    printf "%-40s %5d files read\n", ($d =~ m|([^/]+/[^/]+/[^/]+)$|)[0], scalar @r;
}
sub lines { my $p = shift; open my $f, '<', "$tree/$p" or return 0; my $n = 0; $n++ while <$f>; $n }
my (%dir, $kf, $kl, $df, $dl);
$kf = $kl = $df = $dl = 0;
open my $K, '>', "$out/keep.txt" or die; open my $D, '>', "$out/drop.txt" or die;
for my $p (sort @all) {
    my $n = lines($p);
    if ($keep{$p}) { $kf++; $kl += $n; print $K "$p\n"; next; }
    $df++; $dl += $n; print $D "$p\n";
    my @s = split m|/|, $p;
    my $k = @s > 2 ? "$s[0]/$s[1]/" : @s == 2 ? "$s[0]/" : "(top)";
    $dir{$k}[0]++; $dir{$k}[1] += $n;
}
printf "\nTree: %d files, %d lines\nKeep: %d files, %d lines\nLeft out: %d files, %d lines\n\n",
    $kf + $df, $kl + $dl, $kf, $kl, $df, $dl;
printf "%-40s %6s %9s\n", 'Left out, by directory', 'files', 'lines';
for my $k (sort { $dir{$b}[1] <=> $dir{$a}[1] } keys %dir) {
    printf "%-40s %6d %9d\n", $k, @{$dir{$k}};
}
