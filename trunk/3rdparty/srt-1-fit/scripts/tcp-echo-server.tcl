#!/usr/bin/tclsh

proc SpawnEchoServer {fd host port} {
	fconfigure $fd -encoding binary -translation binary -blocking no -buffering none
	fileevent $fd readable "EchoBack $fd"
	# --- puts stderr "Connected: [fconfigure $fd -peername]"
}

proc EchoBack {fd} {

	# --- puts stderr "READ-READY"

	while 1 {

		# --- puts stderr "READING 4096"
		set r [read $fd 4096]
		if {$r == ""} {
			if {[eof $fd]} {
				# --- puts stderr "EOF. Closing"
				close $fd
				return
			}

			# --- puts stderr "SPURIOUS, giving up read"
			return
		}

		# Set blocking for a short moment of sending
		# in order to prevent losing data that must wait

		# --- puts stderr "SENDING [string bytelength $r] bytes"
		fconfigure $fd -blocking yes
		puts -nonewline $fd $r
		fconfigure $fd -blocking no

		if {[fblocked $fd]} {
			# --- puts stderr "NO MORE DATA"
			# Nothing more to read
			return
		}

		# --- puts stderr "AGAIN"

	}

}

socket -server SpawnEchoServer $argv
puts stderr "SERVER READY"

vwait tk
