#!/usr/bin/tclsh

set read_running 0
set write_running 0
set read_eof 0 
set theend 0

set nread 0
set nwritten 0

proc ReadBack {fd} {

	if { !$::write_running } {
		puts stderr "ERROR: connection closed unexpectedly!"
		set ::theend 1
		return
	}

	set r [read $fd 4096]
	if {$r == ""} {

		if {[eof $fd]} {
			puts stderr "EOF on socket"
			set ::read_running 0
			return
		}

		# --- puts stderr "SPURIOUS, not reading"
		return
	}

	# --- puts stderr "REPRINTING [string bytelength $r] bytes"
	puts -nonewline stdout $r
	incr ::nwritten [string bytelength $r]
	# --- puts stderr "DONE"

	set remain [expr {$::nread - $::nwritten}]
	if { $::read_eof } {
		puts stderr "Finishing... read=$::nread written=$::nwritten diff=[expr {$::nwritten - $::nread}] - [expr {100.0*$remain/$::nread}]%"
	}

	# Nothing more to read
	if {$remain == 0} {
		puts stderr "NOTHING MORE TO BE WRITTEN - exiting"
		set ::theend 1
		return
	}

	after idle "ReadBack $fd"
}

proc SendToSocket {fd} {
	global theend

	if { !$::write_running } {
		# --- puts stderr "SERVER DOWN, not reading"
		fileevent stdin readable {}
		return
	}

	if { $::read_eof } {
		# Don't read, already EOF.

	}
	# --- puts stderr "READING cin"
	set r [read stdin 4096]
	if {$r == ""} {
		if {[eof stdin]} {
			if {!$::read_eof} {
				puts stderr "EOF, setting server off"
				set ::read_eof 1
			}
			# Just enough when the next SendToSocket will
			# not be scheduled.
			return
		}
		# --- puts stderr "SPURIOUS, not reading"
		return
	}

	# --- puts stderr "SENDING [string bytelength $r] bytes"
	# Set blocking for a short moment of sending
	# in order to prevent losing data that must wait
	fconfigure $fd -blocking yes
	puts -nonewline $fd $r
	incr ::nread [string bytelength $r]
	fconfigure $fd -blocking no

    # --- if {[fblocked stdin]} {
    # --- 	# Nothing more to read
    # --- 	return
    # --- }
	after idle "SendToSocket $fd"
}

set fd [socket {*}$argv]
fconfigure $fd -encoding binary -translation binary -blocking no -buffering none
fileevent $fd readable "ReadBack $fd"

fconfigure stdin -encoding binary -translation binary -blocking no
fconfigure stdout -encoding binary -translation binary
fileevent stdin readable "SendToSocket $fd"

# --- puts stderr "READY, sending"
set read_running 1
set write_running 1

vwait theend

close $fd
