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
  the defaut version is a detailed version number (most recent version, number
  of commits since then, short commit SHA).

 .PARAMETER NoPause

  Do not wait for the user to press <enter> at end of execution. By default,
  execute a "pause" instruction at the end of execution, which is useful
  when the script was run from Windows Explorer.

#>
[CmdletBinding()]
param(
    [string]$Version = "",
    [switch]$NoPause = $false
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
$SslRoot = @{
    "x64" = "C:\Program Files\OpenSSL-Win64";
    "Win32" = "C:\Program Files (x86)\OpenSSL-Win32"
}

# Verify OpenSSL directories.
$Missing = 0
foreach ($file in @($SslRoot["x64"], $SslRoot["Win32"])) {
    if (-not (Test-Path $file)) {
        Write-Output "**** Missing $file"
        $Missing = $Missing + 1
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
# Configure and build SRT library using CMake on two architectures.
#-----------------------------------------------------------------------------

foreach ($Platform in @("x64", "Win32")) {

    # Build directory. Cleanup to force a fresh cmake config.
    $BuildDir = "$TmpDir\build.$Platform"
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $BuildDir
    [void](New-Item -Path $BuildDir -ItemType Directory -Force)

    # Run CMake.
    Write-Output "Configuring build for platform $Platform ..."
    $SRoot = $SslRoot[$Platform]
    & $CMake -S $RepoDir -B $BuildDir -A $Platform `
        -DENABLE_STDCXX_SYNC=ON `
        -DOPENSSL_ROOT_DIR="$SRoot" `
        -DOPENSSL_LIBRARIES="$SRoot\lib\libssl_static.lib;$SRoot\lib\libcrypto_static.lib" `
        -DOPENSSL_INCLUDE_DIR="$SRoot\include"

    # Patch version string in version.h
    Get-Content "$BuildDir\version.h" |
        ForEach-Object {
            $_ -replace "#define *SRT_VERSION_STRING .*","#define SRT_VERSION_STRING `"$Version`""
        } |
        Out-File "$BuildDir\version.new" -Encoding ascii
    Move-Item "$BuildDir\version.new" "$BuildDir\version.h" -Force

    # Compile SRT.
    Write-Output "Building for platform $Platform ..."
    foreach ($Conf in @("Release", "Debug")) {
        & $MSBuild "$BuildDir\SRT.sln" /nologo /maxcpucount /property:Configuration=$Conf /property:Platform=$Platform /target:srt_static
    }
}

# Verify the presence of compiled libraries.
Write-Output "Checking compiled libraries ..."
$Missing = 0
foreach ($Conf in @("Release", "Debug")) {
    foreach ($Platform in @("x64", "Win32")) {
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
