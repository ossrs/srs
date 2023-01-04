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
$OpenSSLHomePage = "http://slproweb.com/products/Win32OpenSSL.html"

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

# Get the HTML page for OpenSSL downloads.
$status = 0
$message = ""
try {
    $response = Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $OpenSSLHomePage
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
        Exit-Script "#### Error accessing ${OpenSSLHomePage}: $message"
    }
}

# Parse HTML page to locate the latest MSI files.
$Ref32 = $response.Links.href | Where-Object { $_ -like "*/Win32OpenSSL-*.msi" } | Select-Object -First 1
$Ref64 = $response.Links.href | Where-Object { $_ -like "*/Win64OpenSSL-*.msi" } | Select-Object -First 1

# Build the absolute URL's from base URL (the download page) and href links.
$Url32 = New-Object -TypeName 'System.Uri' -ArgumentList ([System.Uri]$OpenSSLHomePage, $Ref32)
$Url64 = New-Object -TypeName 'System.Uri' -ArgumentList ([System.Uri]$OpenSSLHomePage, $Ref64)

# Download and install one MSI package.
function Download-Install([string]$Url)
{
    $MsiName = (Split-Path -Leaf $Url.toString())
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

# Download and install the two MSI packages.
Download-Install $Url32
Download-Install $Url64
Exit-Script
