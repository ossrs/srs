#!/usr/bin/tclsh

set here [file dirname $argv0] ;# points to [git top]/scripts
set top [file normalize $here/..]

set abichecker [file normalize [file join $top submodules abi-compliance-checker abi-compliance-checker.pl]]

if { ![file exists $abichecker] } {
	puts stderr "Please update submodules first (compliance checker not found in the current view)"
	exit 1
}

# Check if abi-dumper is installed

set abidumper [auto_execok abi-dumper]
if {$abidumper == ""} {
	set installer ""
	foreach ii {zypper dnf apt} {
		if {[auto_execok $ii] != ""} {
			set installer $ii
			break
		}
	}
	if {$installer != ""} {
		puts stderr "ABI dumper not installed. Use such commands to install\n"
		puts stderr " $installer install abi-dumper"
	} else {
		puts stderr "ABI dumper not installed. Find out how to install abi-dumper in your system."
	}
	exit 1
}

# Arguments:
# <abi-check> [directory-with-base] [directory-with-pr]

# NOTE: ABI dump will be done in every build directory as specified.

proc generate-abi-dump {directory lver} {
	set wd [pwd]
	cd $directory
	global top

	# You should have libsrt.so in this directory.
	# Use [glob] to use exception if no file exists
	glob libsrt.so

    exec >@stdout 2>@stderr abi-dumper libsrt.so -o libsrt-abi.dump -public-headers $top/srtcore -lver $lver
	cd $wd
}

set olddir [lindex $argv 0]
set newdir [lindex $argv 1]

if {![file isdirectory $olddir] || ![file isdirectory $newdir]} {
	puts stderr "Wrong arguments. Required <old> <new> build directory"
	exit 1
}

set wd [pwd]
cd $olddir
set base_ver [exec $top/scripts/get-build-version.tcl]
cd $wd
cd $newdir
set new_ver [exec $top/scripts/get-build-version.tcl]
cd $wd

generate-abi-dump $olddir $base_ver
generate-abi-dump $newdir $new_ver

set res [catch {exec >@stdout 2>@stderr $abichecker -l libsrt -old $olddir/libsrt-abi.dump -new $newdir/libsrt-abi.dump} out]

if {$res} {
	puts stderr "ABI compat problems found!!!\nSee the HTML report"
}


