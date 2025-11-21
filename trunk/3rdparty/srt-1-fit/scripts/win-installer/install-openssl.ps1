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

# A bit of history: Where to load OpenSSL binaries from?
#
# The packages are built by "slproweb". The binaries are downloadable from
# their Web page at: http://slproweb.com/products/Win32OpenSSL.html
#
# Initially, the HTML for this page was built on server-side. This script
# grabbed the URL content, parse the HTML and extracted the URL of the
# binaries for the latest OpenSSL packages from the "href" of the links.
#
# At some point in 2024, the site changed policy. The Web page is no longer
# built on server-side, but on client-side. The downloaded HTML contains some
# JavaScript which dynamically builds the URL of the binaries for the latest
# OpenSSL binaries. The previous method now finds no valid package URL from
# the downloaded page (a full browser is needed to execute the JavaScript).
# The JavaScript downloads a JSON file which contains all references to
# the OpenSSL binaries. This script now uses the same method: download that
# JSON file and parse it.

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

# Download and install MSI packages for 32 and 64 bit.
foreach ($bits in @(32, 64)) {

    # Get the URL of the MSI installer from the JSON config.
    $Url = $config.files | Get-Member | ForEach-Object {
        $name = $_.name
        $info = $config.files.$($_.name)
        if (-not $info.light -and $info.installer -like "msi" -and $info.bits -eq $bits -and $info.arch -like "intel") {
            $info.url
        }
    } | Select-Object -Last 1
    if (-not $Url) {
        Exit-Script "#### No MSI installer found for Win${bits}"
    }

    $MsiName = (Split-Path -Leaf $Url)
    $MsiPath = "$TmpDir\$MsiName"

    if (-not $ForceDownload -and (Test-Path $MsiPath)) {
        Write-Output "$MsiName already downloaded, use -ForceDownload to download again"
    }
    else {
        Write-Output "Downloading $Url ..."
        Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $Url -OutFile $MsiPath
    }

    if (-not (Test-Path $MsiPath)) {
        Exit-Script "$Url download failed"
    }

    if (-not $NoInstall) {
        Write-Output "Installing $MsiName"
        Start-Process msiexec.exe -ArgumentList @("/i", $MsiPath, "/qn", "/norestart") -Wait
    }
}

Exit-Script
