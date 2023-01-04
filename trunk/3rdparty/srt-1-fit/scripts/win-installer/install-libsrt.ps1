# SRT library download and install for Windows.
# Copyright (c) 2021, Thierry Lelegard
# All rights reserved.

<#
 .SYNOPSIS

  Download and install the libsrt library for Windows. This script is
  provided to automate the build of Windows applications using libsrt.

 .PARAMETER Destination

  Specify a local directory where the libsrt package will be downloaded.
  By default, use "tmp" subdirectory from this script.

 .PARAMETER ForceDownload

  Force a download even if the package is already downloaded.

 .PARAMETER GitHubActions

  When used in a GitHub Actions workflow, make sure that the LIBSRT
  environment variable is propagated to subsequent jobs.

 .PARAMETER NoInstall

  Do not install the package. By default, libsrt is installed.

 .PARAMETER NoPause

  Do not wait for the user to press <enter> at end of execution. By default,
  execute a "pause" instruction at the end of execution, which is useful
  when the script was run from Windows Explorer.
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [string]$Destination = "",
    [switch]$ForceDownload = $false,
    [switch]$GitHubActions = $false,
    [switch]$NoInstall = $false,
    [switch]$NoPause = $false
)

Write-Output "libsrt download and installation procedure"

# Default directory for downloaded products.
if (-not $Destination) {
    $Destination = "$PSScriptRoot\tmp"
}

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

# Without this, Invoke-WebRequest is awfully slow.
$ProgressPreference = 'SilentlyContinue'

# Get the URL of the latest libsrt installer.
$URL = (Invoke-RestMethod "https://api.github.com/repos/Haivision/srt/releases?per_page=20" |
        ForEach-Object { $_.assets } |
        ForEach-Object { $_.browser_download_url } |
        Select-String @("/libsrt-.*\.exe$", "/libsrt-.*-win-installer\.zip$") |
        Select-Object -First 1)

if (-not $URL) {
    Exit-Script "Could not find a libsrt installer on GitHub"
}
if (-not ($URL -match "\.zip$") -and -not ($URL -match "\.exe$")) {
    Exit-Script "Unexpected URL, not .exe, not .zip: $URL"
}

# Installer name and path.
$InstName = (Split-Path -Leaf $URL)
$InstPath = "$Destination\$InstName"

# Create the directory for downloaded products when necessary.
[void](New-Item -Path $Destination -ItemType Directory -Force)

# Download installer
if (-not $ForceDownload -and (Test-Path $InstPath)) {
    Write-Output "$InstName already downloaded, use -ForceDownload to download again"
}
else {
    Write-Output "Downloading $URL ..."
    Invoke-WebRequest $URL.ToString() -UseBasicParsing -UserAgent Download -OutFile $InstPath
    if (-not (Test-Path $InstPath)) {
        Exit-Script "$URL download failed"
    }
}

# If installer is an archive, expect an exe with same name inside.
if ($InstName -match "\.zip$") {

    # Expected installer name in archive.
    $ZipName = $InstName
    $ZipPath = $InstPath
    $InstName = $ZipName -replace '-win-installer.zip','.exe'
    $InstPath = "$Destination\$InstName"

    # Extract the installer.
    Remove-Item -Force $InstPath -ErrorAction SilentlyContinue
    Write-Output "Expanding $ZipName ..."
    Expand-Archive $ZipPath -DestinationPath $Destination
    if (-not (Test-Path $InstPath)) {
        Exit-Script "$InstName not found in $ZipName"
    }
}

# Install libsrt
if (-not $NoInstall) {
    Write-Output "Installing $InstName"
    Start-Process -FilePath $InstPath -ArgumentList @("/S") -Wait
}

# Propagate LIBSRT in next jobs for GitHub Actions.
if ($GitHubActions -and (-not -not $env:GITHUB_ENV) -and (Test-Path $env:GITHUB_ENV)) {
    $libsrt = [System.Environment]::GetEnvironmentVariable("LIBSRT","Machine")
    Write-Output "LIBSRT=$libsrt" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
}

Exit-Script
