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

  Download, expand and install NSIS, the NullSoft Installer Scripting.

 .PARAMETER ForceDownload

  Force a download even if NSIS is already downloaded.

 .PARAMETER NoInstall

  Do not install the NSIS package. By default, NSIS is installed.

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

Write-Output "NSIS download and installation procedure"
$NSISPage = "https://nsis.sourceforge.io/Download"
$FallbackURL = "http://prdownloads.sourceforge.net/nsis/nsis-3.05-setup.exe?download"

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

# Get the HTML page for NSIS downloads.
$status = 0
$message = ""
$Ref = $null
try {
    $response = Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $NSISPage
    $status = [int] [Math]::Floor($response.StatusCode / 100)
}
catch {
    $message = $_.Exception.Message
}

if ($status -ne 1 -and $status -ne 2) {
    # Error fetch NSIS download page.
    if ($message -eq "" -and (Test-Path variable:response)) {
        Write-Output "Status code $($response.StatusCode), $($response.StatusDescription)"
    }
    else {
        Write-Output "#### Error accessing ${NSISPage}: $message"
    }
}
else {
    # Parse HTML page to locate the latest installer.
    $Ref = $response.Links.href | Where-Object { $_ -like "*/nsis-*-setup.exe?download" } | Select-Object -First 1
}

if (-not $Ref) {
    # Could not find a reference to NSIS installer.
    $Url = [System.Uri]$FallbackURL
}
else {
    # Build the absolute URL's from base URL (the download page) and href links.
    $Url = New-Object -TypeName 'System.Uri' -ArgumentList ([System.Uri]$NSISPage, $Ref)
}

$InstallerName = (Split-Path -Leaf $Url.LocalPath)
$InstallerPath = "$TmpDir\$InstallerName"

# Download installer
if (-not $ForceDownload -and (Test-Path $InstallerPath)) {
    Write-Output "$InstallerName already downloaded, use -ForceDownload to download again"
}
else {
    Write-Output "Downloading $Url ..."
    Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $Url -OutFile $InstallerPath
    if (-not (Test-Path $InstallerPath)) {
        Exit-Script "$Url download failed"
    }
}

# Install NSIS
if (-not $NoInstall) {
    Write-Output "Installing $InstallerName"
    Start-Process -FilePath $InstallerPath -ArgumentList @("/S") -Wait
}

Exit-Script
