#-----------------------------------------------------------------------------
#
#  SRT - Secure, Reliable, Transport
#  Copyright (c) 2021-2024, Thierry Lelegard
# 
#  This Source Code Form is subject to the terms of the Mozilla Public
#  License, v. 2.0. If a copy of the MPL was not distributed with this
#  file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
#-----------------------------------------------------------------------------

<#
 .SYNOPSIS

  Download, expand and install OpenSSL for Windows.

 .PARAMETER ForceDownload

  Force a download even if the OpenSSL installers are already downloaded.

 .PARAMETER NoInstall

  Do not install the OpenSSL packages. By default, OpenSSL is installed.

 .PARAMETER NoPause

  Do not wait for the user to press <enter> at end of execution. By default,
  execute a "pause" instruction at the end of execution, which is useful
  when the script was run from Windows Explorer.
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [switch]$ForceDownload = $false,
    [switch]$NoInstall = $false,
    [switch]$NoPause = $false
)

Write-Output "OpenSSL download and installation procedure"

# The list of OpenSSL packages is available in a JSON file. See more details in README.md.
$PackageList = "https://github.com/slproweb/opensslhashes/raw/master/win32_openssl_hashes.json"

# A function to exit this script.
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

# Local file names.
$RootDir = $PSScriptRoot
$TmpDir = "$RootDir\tmp"

# Create the directory for external products when necessary.
[void] (New-Item -Path $TmpDir -ItemType Directory -Force)

# Without this, Invoke-WebRequest is awfully slow.
$ProgressPreference = 'SilentlyContinue'

# Get the JSON configuration file for OpenSSL downloads.
$status = 0
$message = ""
try {
    $response = Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $PackageList
    $status = [int] [Math]::Floor($response.StatusCode / 100)
}
catch {
    $message = $_.Exception.Message
}
if ($status -ne 1 -and $status -ne 2) {
    if ($message -eq "" -and (Test-Path variable:response)) {
        Exit-Script "Status code $($response.StatusCode), $($response.StatusDescription)"
    }
    else {
        Exit-Script "#### Error accessing ${PackageList}: $message"
    }
}
$config = ConvertFrom-Json $Response.Content

# Find the URL of the latest "universal" installer in the JSON config file.
$Url = $config.files | Get-Member | ForEach-Object {
    $name = $_.name
    $info = $config.files.$($_.name)
    if (-not $info.light -and $info.installer -like "exe" -and $info.arch -like "universal") {
        $info.url
    }
} | Select-Object -Last 1
if (-not $Url) {
    Exit-Script "#### No universal installer found"
}

$ExeName = (Split-Path -Leaf $Url)
$ExePath = "$TmpDir\$ExeName"

if (-not $ForceDownload -and (Test-Path $ExePath)) {
    Write-Output "$ExeName already downloaded, use -ForceDownload to download again"
}
else {
    Write-Output "Downloading $Url ..."
    Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $Url -OutFile $ExePath
}

if (-not (Test-Path $ExePath)) {
    Exit-Script "$Url download failed"
}

if (-not $NoInstall) {
    Write-Output "Installing $ExeName"
    Start-Process -FilePath $ExePath -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/ALLUSERS") -Wait
}

Exit-Script
