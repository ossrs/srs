#!/usr/bin/tclsh
#*
#* SRT - Secure, Reliable, Transport
#* Copyright (c) 2020 Haivision Systems Inc.
#* 
#* This Source Code Form is subject to the terms of the Mozilla Public
#* License, v. 2.0. If a copy of the MPL was not distributed with this
#* file, You can obtain one at http://mozilla.org/MPL/2.0/.
#* 
#*/
#
#*****************************************************************************
#written by
#  Haivision Systems Inc.
#*****************************************************************************

# What fields are there in every entry
set model {
	longname
	shortname
	id
	description
}

# Logger definitions.
# Comments here allowed, just only for the whole line.

# Use values greater than 0. Value 0 is reserved for LOGFA_GENERAL,
# which is considered always enabled.
set loggers {
	GENERAL    gg  0  "General uncategorized log, for serious issues only"
	SOCKMGMT   sm  1  "Socket create/open/close/configure activities"
	CONN       cn  2  "Connection establishment and handshake"
	XTIMER     xt  3  "The checkTimer and around activities"
	TSBPD      ts  4  "The TsBPD thread"
	RSRC       rs  5  "System resource allocation and management"
	CONGEST    cc  7  "Congestion control module"
	PFILTER    pf  8  "Packet filter module"
	API_CTRL   ac  11 "API part for socket and library managmenet"
	QUE_CTRL   qc  13 "Queue control activities"
	EPOLL_UPD  ei  16 "EPoll, internal update activities"

	API_RECV   ar  21 "API part for receiving"
	BUF_RECV   br  22 "Buffer, receiving side"
	QUE_RECV   qr  23 "Queue, receiving side"
	CHN_RECV   kr  24 "CChannel, receiving side"
	GRP_RECV   gr  25 "Group, receiving side"

	API_SEND   as  31 "API part for sending"
	BUF_SEND   bs  32 "Buffer, sending side"
	QUE_SEND   qs  33 "Queue, sending side"
	CHN_SEND   ks  34 "CChannel, sending side"
	GRP_SEND   gs  35 "Group, sending side"

	INTERNAL   in  41 "Internal activities not connected directly to a socket"
	QUE_MGMT   qm  43 "Queue, management part"
	CHN_MGMT   km  44 "CChannel, management part"
	GRP_MGMT   gm  45 "Group, management part"
	EPOLL_API  ea  46 "EPoll, API part"
}

set hidden_loggers {
	# Haicrypt logging - usually off.
	HAICRYPT hc 6  "Haicrypt module area"
 
    # defined in apps, this is only a stub to lock the value
	APPLOG   ap 10 "Applications"
}

set globalheader {
 /*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */

}


# This defines, what kind of definition will be generated
# for a given file out of the log FA entry list.

# Fields:
#  - prefix/postfix model
#  - logger_format
#  - hidden_logger_format

# COMMENTS NOT ALLOWED HERE! Only as C++ comments inside C++ model code.
set special {
	srtcore/logger_default.cpp {
		if {"$longname" == "HAICRYPT"} {
			puts $od "
#if ENABLE_HAICRYPT_LOGGING
		allfa.set(SRT_LOGFA_HAICRYPT, true);
#endif"
		}
	}
}

proc GenerateModelForSrtH {} {

	# `path` will be set to the git top path
	global path

	set fd [open [file join $path srtcore/srt.h] r]

	set contents ""

	set state read
	set pass looking

	while { [gets $fd line] != -1 } {
		if { $state == "read" } {

			if { $pass != "passed" } {

				set re [regexp {SRT_LOGFA BEGIN GENERATED SECTION} $line]
				if {$re} {
					set state skip
					set pass found
				}

			}

			append contents "$line\n"
			continue
		}

		if {$state == "skip"} {
			if { [string trim $line] == "" } {
				# Empty line, continue skipping
				continue
			}

			set re [regexp {SRT_LOGFA END GENERATED SECTION} $line]
			if {!$re} {
				# Still SRT_LOGFA definitions
				continue
			}

			# End of generated section. Switch back to pass-thru.

			# First fill the gap
			append contents "\n\$entries\n\n"

			append contents "$line\n"
			set state read
			set pass passed
		}
	}

	close $fd

	# Sanity check
	if {$pass != "passed"} {
		error "Invalid contents of `srt.h` file, can't find '#define SRT_LOGFA_' phrase"
	}

	return $contents
}

# COMMENTS NOT ALLOWED HERE! Only as C++ comments inside C++ model code.
# (NOTE: Tcl syntax highlighter will likely falsely highlight # as comment here)
#
# Model:  TARGET-NAME { format-model logger-pattern hidden-logger-pattern }
#
# Special syntax:
#
# %<command> : a high-level command execution. This declares a command that
# must be executed to GENERATE the model. Then, [subst] is executed
# on the results.
#
# = : when placed as the hidden-logger-pattern, it's equal to logger-pattern.
#
set generation {
	srtcore/srt.h {

		{%GenerateModelForSrtH}

		{#define [format "%-20s %-3d" SRT_LOGFA_${longname} $id] // ${shortname}log: $description}

		=
	}

    srtcore/logger_default.cpp {

        {
            $globalheader
            #include "srt.h"
            #include "logging.h"
            #include "logger_defs.h"

            namespace srt_logging
            {
                AllFaOn::AllFaOn()
                {
                    $entries
                }
            } // namespace srt_logging

        }

        {
            allfa.set(SRT_LOGFA_${longname}, true);
        }
    }

    srtcore/logger_defs.cpp {

        {
            $globalheader
            #include "srt.h"
            #include "logging.h"
            #include "logger_defs.h"

            namespace srt_logging { AllFaOn logger_fa_all; }
            // We need it outside the namespace to preserve the global name.
            // It's a part of "hidden API" (used by applications)
            SRT_API srt_logging::LogConfig srt_logger_config(srt_logging::logger_fa_all.allfa);

            namespace srt_logging
            {
                $entries
            } // namespace srt_logging
        }

        {
            Logger ${shortname}log(SRT_LOGFA_${longname}, srt_logger_config, "SRT.${shortname}");
        }
    }

    srtcore/logger_defs.h {
        {
            $globalheader
            #ifndef INC_SRT_LOGGER_DEFS_H
            #define INC_SRT_LOGGER_DEFS_H

            #include "srt.h"
            #include "logging.h"

            namespace srt_logging
            {
                struct AllFaOn
                {
                    LogConfig::fa_bitset_t allfa;
                    AllFaOn();
                };

                $entries

            } // namespace srt_logging

            #endif
        }

        {
            extern Logger ${shortname}log;
        }
    }

    apps/logsupport_appdefs.cpp {
        {
            $globalheader
            #include "logsupport.hpp"

            LogFANames::LogFANames()
            {
                $entries
            }
        }

        {
            Install("$longname", SRT_LOGFA_${longname});
        }

        {
            Install("$longname", SRT_LOGFA_${longname});
        }
    }
}

# EXECUTION

set here [file dirname [file normalize $argv0]]

if {[lindex [file split $here] end] != "scripts"} {
	puts stderr "The script is in weird location."
	exit 1
}

set path [file join {*}[lrange [file split $here] 0 end-1]]

# Utility. Allows to put line-oriented comments and have empty lines
proc no_comments {input} {
	set output ""
	foreach line [split $input \n] {
		set nn [string trim $line]
		if { $nn == "" || [string index $nn 0] == "#" } {
			continue
		}
		append output $line\n
	}

	return $output
}

proc generate_file {od target} {

	global globalheader
	lassign [dict get $::generation $target] format_model pattern hpattern

    set ptabprefix ""

	if {[string index $format_model 0] == "%"} {
		set command [string range $format_model 1 end]
		set format_model [eval $command]
	}

	if {$format_model != ""} {
		set beginindex 0
		while { [string index $format_model $beginindex] == "\n" } {
			incr beginindex
		}

		set endindex $beginindex
		while { [string is space [string index $format_model $endindex]] } {
			incr endindex
		}

		set tabprefix [string range $pattern $beginindex $endindex-1]

		set newformat ""
		foreach line [split $format_model \n] {
			if {[string trim $line] == ""} {
				append newformat "\n"
				continue
			}

			if {[string first $tabprefix $line] == 0} {
                set line [string range $line [string length $tabprefix] end]
			} 
            append newformat $line\n

            set ie [string first {$} $line]
            if {$ie != -1} {
                if {[string range $line $ie end] == {$entries}} {
                    set ptabprefix "[string range $line 0 $ie-1]"
                }
            }
		}

		set format_model $newformat
		unset newformat
	}

	set entries ""

	if {[string trim $pattern] != "" } {

        set prevval 0
 		set pattern [string trim $pattern]

		# The first "$::model" will expand into variable names
		# as defined there.
		foreach [list {*}$::model] [no_comments $::loggers] {
			if {$prevval + 1 != $id} {
				append entries "\n"
			}

			append entries "${ptabprefix}[subst -nobackslashes $pattern]\n"
			set prevval $id
		}
	}

	if {$hpattern != ""} {
		if {$hpattern == "="} {
			set hpattern $pattern
		} else {
 			set hpattern [string trim $hpattern]
		}

		# Extra line to separate from the normal entries
		append entries "\n"
		foreach [list {*}$::model] [no_comments $::hidden_loggers] {
			append entries "${ptabprefix}[subst -nobackslashes $hpattern]\n"
		}
	}

	if { [dict exists $::special $target] } {
		set code [subst [dict get $::special $target]]

		# The code should contain "append entries" !
		eval $code
	}

	set entries [string trim $entries]

    if {$format_model == ""} {
        set format_model $entries
    }

	# For any case, cut external spaces
	puts $od [string trim [subst -nocommands -nobackslashes $format_model]]
}

proc debug_vars {list} {
	set output ""
	foreach name $list {
		upvar $name _${name}
		lappend output "${name}=[set _${name}]"
	}

	return $output
}

# MAIN

set entryfiles $argv

if {$entryfiles == ""} {
	set entryfiles [dict keys $generation]
} else {
	foreach ef $entryfiles {
		if { $ef ni [dict keys $generation] } {
			error "Unknown generation target: $entryfiles"
		}
	}
}

foreach f $entryfiles {

	# Set simple relative path, if the file isn't defined as path.
	if { [llength [file split $f]] == 1 } {
		set filepath $f
	} else {
		set filepath [file join $path $f] 
	}

    puts stderr "Generating '$filepath'"
	set od [open $filepath.tmp w]
	generate_file $od $f
	close $od
	if { [file exists $filepath] } {
		puts "WARNING: will overwrite exiting '$f'. Hit ENTER to confirm, or Control-C to stop"
		gets stdin
	}

	file rename -force $filepath.tmp $filepath
}

puts stderr Done.

