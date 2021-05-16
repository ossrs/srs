#!/usr/bin/tclsh

set cachefile [lindex $argv 0]

if { $cachefile == "" } {
	puts stderr "Usage: [file tail $argv0] <existing CMakeCache.txt file>"
	exit 1
}

set struct {
	name
	type
	value
	description
}

set fd [open $cachefile r]

set cached ""

set dbase ""

while {[gets $fd line] != -1 } {
	set line [string trim $line]

	# Hash comment
	if { [string index $line 0] == "#" } {
		continue
	}

	# empty line
	if { $line == "" } {
		set cached ""
		continue
	}

	if { [string range $line 0 1] == "//" } {
		set linepart [string range $line 2 end]
		# Variable description. Add to cache.
		if { $cached != "" && [string index $cached end] != " " && [string index $linepart 0] != " " } {
			append cached " "
		}
		append cached $linepart
	}

	# Possibly a variable
	if [string is alpha [string index $line 0]] {
		# Note: this skips variables starting grom underscore.

		if { [string range $line 0 5] == "CMAKE_" } {
			# Skip variables with CMAKE_ prefix, they are internal.
			continue
		}

		lassign [split $line =] vartype value
		lassign [split $vartype :] var type

		# Store the variable now
		set storage [list $var $type $value $cached]
		set cached ""
		lappend dbase $storage

		continue
	}

	#puts stderr "Ignored line: $line"

	# Ignored.
}

# Now look over the stored variables

set lenlimit 80

foreach stor $dbase {

	lassign $stor {*}$struct

	if { [string length $description] > $lenlimit } {
		set description [string range $description 0 $lenlimit-2]...
	}

	if { $type in {STATIC INTERNAL} } {
		continue
	}

	# Check special case of CXX to turn back to c++.
	set pos [string first CXX $name]
	if { $pos != -1 } {
		# Check around, actually after XX should be no letter.
		if { $pos+3 >= [string length $name] || ![string is alpha [string index $name $pos+3]] } {
			set name [string replace $name $pos $pos+2 C++]
		}
	}

	set optname [string tolower [string map {_ -} $name]]

	# Variables of type bool are just empty.
	# Variables of other types must have =<value> added.
	# Lowercase cmake type will be used here.
	set optassign ""
	set def ""
	if { $type != "BOOL" } {
		set optassign "=<[string tolower $type]>"
	} else {
		# Supply default for boolean option
		set def " (default: $value)"
	}

	puts "    $optname$optassign \"$description$def\""
}




