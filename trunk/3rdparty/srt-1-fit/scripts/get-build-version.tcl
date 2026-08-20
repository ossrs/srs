#!/usr/bin/tclsh

if {![file exists version.h]} {
	puts stderr "No version.h file found - run this in the build directory!"
	exit 1
}

set fd [open version.h r]

set version ""
set line ""
while {[gets $fd line] != -1} {
	# Use regexp because this is safer to avoid
	# unexpected list interpretation mistakes
	if {[regexp {#define SRT_VERSION_STRING} $line]} {
		# Once matched this way, we can safely use tcl-list-like access
		set version [lindex $line 2 0]
		break
	}
}
close $fd

if {$version == ""} {
	puts stderr "No version extracted (no SRT_VERSION_STRING found)"
	exit 1
}

lassign $argv model part

if {$model == ""} {
	set model current
}

lassign [split $version .] major minor patch

if {$part == "minor"} {
	set prefix "$minor"
} else {
	set prefix "$major.$minor"
}

if {$model == "base"} {
	puts "$prefix.0"
} else {
	puts "$prefix.$patch"
}

