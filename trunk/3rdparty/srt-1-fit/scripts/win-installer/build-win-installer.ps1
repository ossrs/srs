#-----------------------------------------------------------------------------
#
#  SRT - Secure, Reliable, Transport
#  Copyright (c) 2021, Thierry Lelegard
# 
#  This Source Code Form is subject to the terms of the Mozilla Public
#  License, v. 2.0. If a copy of the MPL was not distributed with this
#  file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
#-----------------------------------------------------------------------------

<#
 .SYNOPSIS

  Build the SRT static libraries installer for Windows.

 .PARAMETER Version

  Use the specified string as version number from libsrt. By default, if
  the current commit has a tag, use that tag (initial 'v' removed). Otherwise,
  the default version is a detailed version number (most recent version, number
  of commits since then, short commit SHA).

 .PARAMETER NoBuild

  Do not rebuild the SRT libraries. Assume that they are already built.
  Only build the installer.

 .PARAMETER NoPause

  Do not wait for the user to press <enter> at end of execution. By default,
  execute a "pause" instruction at the end of execution, which is useful
  when the script was run from Windows Explorer.

#>
[CmdletBinding()]
param(
    [string]$Version = "",
    [switch]$NoBuild = $false,
    [switch]$NoPause = $false,
	[string]$OnlyPlatform = ""
)
Write-Output "Building the SRT static libraries installer for Windows"

# Directory containing this script:
$ScriptDir = $PSScriptRoot

# The root of the srt repository is two levels up.
$RepoDir = (Split-Path -Parent (Split-Path -Parent $ScriptDir))

# Output directory for final installers:
$OutDir = "$ScriptDir\installers"

# Temporary directory for build operations:
$TmpDir = "$ScriptDir\tmp"


#-----------------------------------------------------------------------------
# A function to exit this script with optional error message, using -NoPause
#-----------------------------------------------------------------------------

function Exit-Script([string]$Message = "")
{
    $Code = 0
    if ($Message -ne "") {
        Write-Output "ERROR: $Message"
        $Code = 1
    }
    if (-not $NoPause) {
        pause
    }
    exit $Code
}


#-----------------------------------------------------------------------------
# Build SRT version strings
#-----------------------------------------------------------------------------

# By default, let git format a decent version number.
if (-not $Version) {
    $Version = (git describe --tags ) -replace '-g','-'
}
$Version = $Version -replace '^v',''

# Split version string in pieces and make sure to get at least four elements.
$VField = ($Version -split "[-\. ]") + @("0", "0", "0", "0") | Select-String -Pattern '^\d*$'
$VersionInfo = "$($VField[0]).$($VField[1]).$($VField[2]).$($VField[3])"

Write-Output "SRT version: $Version"
Write-Output "Windows version info: $VersionInfo"


#-----------------------------------------------------------------------------
# Initialization phase, verify prerequisites
#-----------------------------------------------------------------------------

# Locate OpenSSL root from local installation.
# We now requires the "universal" OpenSSL installer (see README.md).
# The universal OpenSSL installer is installed in $SSL.
# $ARCH translates between VC "platform" names to subdirectory names inside $SSL.
# $LIBDIR translates between VC "configuration" names to library subdirectory names.

$SSL = "C:\Program Files (x86)\OpenSSL-WinUniversal"
$ARCH = @{x64 = "x64"; Win32 = "x86"; ARM64 = "arm64"}
$LIBDIR = @{Release = "MD"; Debug = "MDd"}
$LIBFILE = @{crypto = "libcrypto_static.lib"; ssl = "libssl_static.lib"}

function ssl-include($pf) { return "$SSL\include\$($ARCH.$pf)" }
function ssl-libdir($pf, $conf) { return "$SSL\lib\VC\$($ARCH.$pf)\$($LIBDIR.$conf)" }
function ssl-lib($pf, $conf, $lib) { return "$(ssl-libdir $pf $conf)\$($LIBFILE.$lib)" }

# Verify OpenSSL directories and static libraries.
Write-Output "Searching OpenSSL libraries ..."
$Missing = 0
foreach ($Platform in $SSL.Keys) {
	if ($OnlyPlatform -ne "" -and $Platform -ne $OnlyPlatform) {
		continue
	}
    if (-not (Test-Path -PathType Container $(ssl-include $Platform))) {
        Write-Output "**** Missing $(ssl-include $Platform)"
        $Missing = $Missing + 1
    }
    foreach ($lib in $LIBFILE.Keys) {
        foreach ($conf in $LIBDIR.Keys) {
            if (-not (Test-Path $(ssl-lib $Platform $conf $lib))) {
                Write-Output "**** Missing $(ssl-lib $Platform $conf $lib)"
                $Missing = $Missing + 1
            }
        }
    }
}
if ($Missing -gt 0) {
    Exit-Script "Missing $Missing OpenSSL files, use install-openssl.ps1 to install OpenSSL"
}

# Locate MSBuild and CMake, regardless of Visual Studio version.
Write-Output "Searching MSBuild ..."
$MSRoots = @("C:\Program Files*\MSBuild", "C:\Program Files*\Microsoft Visual Studio", "C:\Program Files*\CMake*")
$MSBuild = Get-ChildItem -Recurse -Path $MSRoots -Include MSBuild.exe -ErrorAction Ignore |
    ForEach-Object { (Get-Command $_).FileVersionInfo } |
    Sort-Object -Unique -Property FileVersion |
    ForEach-Object { $_.FileName} |
    Select-Object -Last 1
if (-not $MSBuild) {
    Exit-Script "MSBuild not found"
}

Write-Output "Searching CMake ..."
$CMake = Get-ChildItem -Recurse -Path $MSRoots -Include cmake.exe -ErrorAction Ignore |
    ForEach-Object { (Get-Command $_).FileVersionInfo } |
    Sort-Object -Unique -Property FileVersion |
    ForEach-Object { $_.FileName} |
    Select-Object -Last 1
if (-not $CMake) {
    Exit-Script "CMake not found, check option 'C++ CMake tools for Windows' in Visual Studio installer"
}

# Locate NSIS, the Nullsoft Scriptable Installation System.
Write-Output "Searching NSIS ..."
$NSIS = Get-Item "C:\Program Files*\NSIS\makensis.exe" | ForEach-Object { $_.FullName} | Select-Object -Last 1
if (-not $NSIS) {
    Exit-Script "NSIS not found, use install-nsis.ps1 to install NSIS"
}

Write-Output "MSBuild: $MSBuild"
Write-Output "CMake: $CMake"
Write-Output "NSIS: $NSIS"

# Create the directories for builds when necessary.
[void](New-Item -Path $TmpDir -ItemType Directory -Force)
[void](New-Item -Path $OutDir -ItemType Directory -Force)


#-----------------------------------------------------------------------------
# Configure and build SRT library using CMake on all architectures.
#-----------------------------------------------------------------------------

if (-not $NoBuild) {
	Write-Output "BUILD ENABLED - configuring build"
	if ($OnlyPlatform -ne "") {
		Write-Output "LIMIT TO PLATFORM: $OnlyPlatform"
	}
    foreach ($Platform in $ARCH.Keys) {
		if ($OnlyPlatform -ne "" -and $Platform -ne $OnlyPlatform) {
			Write-Output "Skipping platform $Platform."
			continue
		}
		Write-Output "Preparing platform $Platform."
        # Build directory. Cleanup to force a fresh cmake config.
        $BuildDir = "$TmpDir\build.$Platform"
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $BuildDir
        [void](New-Item -Path $BuildDir -ItemType Directory -Force)

        # Run CMake.
        Write-Output "Configuring build for platform $Platform ..."
		$params = @("-S", "$RepoDir",
					"-B", "$BuildDir",
					"-A", "$Platform",
					"-DENABLE_STDCXX_SYNC=ON",
					"-DOPENSSL_INCLUDE_DIR=$(ssl-include $Platform)",
					"-DOPENSSL_ROOT_DIR=$(ssl-libdir $Platform Release)"
					)
		Write-Output "Running: cmake $params"
        & $CMake $params
        # Patch version string in version.h
        Get-Content "$BuildDir\version.h" |
            ForEach-Object {
                $_ -replace "#define *SRT_VERSION_STRING .*","#define SRT_VERSION_STRING `"$Version`""
            } |
            Out-File "$BuildDir\version.new" -Encoding ascii
        Move-Item "$BuildDir\version.new" "$BuildDir\version.h" -Force

        # Compile SRT.
        Write-Output "Building for platform $Platform ..."
        foreach ($Conf in $LIBDIR.Keys) {
           # The solution file format depends on the CMake version.
           # Try new XML solution file format first.
           $SolFile = "$BuildDir\SRT.slnx"
           if (-not (Test-Path $SolFile)) {
               # Try legacy solution file format.
               $SolFile = "$BuildDir\SRT.sln"
           }
           if (-not (Test-Path $SolFile)) {
               Exit-Script "Solution file $BuildDir\SRT.sln[x] not found, check CMake output"
           }
           & $MSBuild $SolFile /nologo /maxcpucount /property:Configuration=$Conf /property:Platform=$Platform /target:srt_static
        }
    }
}

# Verify the presence of compiled libraries.
Write-Output "Checking compiled libraries ..."
$Missing = 0
foreach ($Conf in $LIBDIR.Keys) {
    foreach ($Platform in $SSL.Keys) {
		if ($OnlyPlatform -ne "" -and $Platform -ne $OnlyPlatform) {
			continue
		}
        $Path = "$TmpDir\build.$Platform\$Conf\srt_static.lib"
        if (-not (Test-Path $Path)) {
            Write-Output "**** Missing $Path"
            $Missing = $Missing + 1
        }
    }
}
if ($Missing -gt 0) {
    Exit-Script "Missing $Missing files"
}


#-----------------------------------------------------------------------------
# Build the binary installer.
#-----------------------------------------------------------------------------

$InstallExe = "$OutDir\libsrt-$Version.exe"
$InstallZip = "$OutDir\libsrt-$Version-win-installer.zip"

Write-Output "Building installer ..."
& $NSIS /V2 `
    /DVersion="$Version" `
    /DVersionInfo="$VersionInfo" `
    /DOutDir="$OutDir" `
    /DBuildRoot="$TmpDir" `
    /DRepoDir="$RepoDir" `
    /DlibsslWin32Release="$(ssl-lib Win32 Release ssl)" `
    /DlibsslWin32Debug="$(ssl-lib Win32 Debug ssl)" `
    /DlibcryptoWin32Release="$(ssl-lib Win32 Release crypto)" `
    /DlibcryptoWin32Debug="$(ssl-lib Win32 Debug crypto)" `
    /DlibsslWin64Release="$(ssl-lib x64 Release ssl)" `
    /DlibsslWin64Debug="$(ssl-lib x64 Debug ssl)" `
    /DlibcryptoWin64Release="$(ssl-lib x64 Release crypto)" `
    /DlibcryptoWin64Debug="$(ssl-lib x64 Debug crypto)" `
    /DlibsslArm64Release="$(ssl-lib Arm64 Release ssl)" `
    /DlibsslArm64Debug="$(ssl-lib Arm64 Debug ssl)" `
    /DlibcryptoArm64Release="$(ssl-lib Arm64 Release crypto)" `
    /DlibcryptoArm64Debug="$(ssl-lib Arm64 Debug crypto)" `
    "$ScriptDir\libsrt.nsi" 

if (-not (Test-Path $InstallExe)) {
    Exit-Script "**** Missing $InstallExe"
}

Write-Output "Building installer archive ..."
Remove-Item -Force -ErrorAction SilentlyContinue $InstallZip
Compress-Archive -Path $InstallExe -DestinationPath $InstallZip -CompressionLevel Optimal

if (-not (Test-Path $InstallZip)) {
    Exit-Script "**** Missing $InstallZip"
}

Exit-Script
