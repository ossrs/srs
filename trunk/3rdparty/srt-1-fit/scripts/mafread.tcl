#!/usr/bin/tclsh

proc is-section line {
	return [regexp {^[A-Z ]+$} $line]
}

# First argument is Manifest file, others are sections.
set sections [lassign $argv maffile]

if { $sections == "" } {
	puts stderr "Usage: [file tail $argv0] <MAF file> <section name>"
	exit 1
}

# NOTE: If the file doesn't exist, simply print nothing.
# If there's no manifest file under this name, it means that
# there are no files that satisfy given manifest and section.
if { [catch {set fd [open $maffile r]}] } {
	exit
}

set extracted ""
set insection 0

while { [gets $fd line] >= 0 } {
	set oline [string trim $line]
	if { $oline == "" } {
		continue
	}

	if { [string index $oline 0] == "#" } {
		continue
	}

	if { !$insection } {
		# An opportunity to see if this is a section name
		if { ![is-section $line] } {
			continue
		}

		# If it is, then check if this is OUR section
		if { $oline in $sections } {
			set insection 1
			continue
		}
	} else {
		# We are inside the interesting section, so collect filenames
		# Check if this is a next section name - if it is, stop reading.
		if { [is-section $line] } {
			continue
		}

		# Otherwise read the current filename
		lappend extracted $oline
	}
}

puts $extracted
