# SRT Static Libraries Installer for Windows

This directory contains scripts to build a binary installer for
libsrt on Windows systems for Visual Studio applications using SRT.

## SRT developer: Building the libsrt installer

### Prerequisites

These first two steps need to be executed once only.

- Prerequisite 1: Install OpenSSL for Windows, both 64 and 32 bits.
  This can be done automatically by running the PowerShell script `install-openssl.ps1`.

- Prerequisite 2: Install NSIS, the NullSoft Installation Scripting system.
  This can be done automatically by running the PowerShell script `install-nsis.ps1`.

### Building the libsrt installer

To build the libsrt installer, simply run the PowerShell script `build-win-installer.ps1`.
Running it without parameters, for instance launching it from the Windows Explorer, is
sufficient to build the installer.

Optional parameters:

- `-Version name` :
  Use the specified string as version number for libsrt. By default, if the
  current commit has a tag, use that tag (initial "v" removed, for instance
  `1.4.3`). Otherwise, the defaut version is a detailed version number (most
  recent version, number of commits since then, short commit SHA, for instance
  `1.4.3-32-g22cc924`). Use that option if necessary to specify some other
  non-standard form of version string.
  
- `-NoPause` :
  Do not wait for the user to press `<enter>` at end of execution. By default,
  execute a `pause` instruction at the end of execution, which is useful
  when the script was launched from Windows Explorer. Use that option when the
  script is invoked from another PowerShell script.

The installer is then available in the directory `installers`.

The name of the installer is `libsrt-VERS.exe` where `VERS` is the SRT version number
(see the `-Version` option).

The installer shall then be published as a release asset in the `srt` repository
on GitHub, either as `libsrt-VERS.exe` or `libsrt-VERS-win-installer.zip`.
In the latter case, the archive shall contain `libsrt-VERS.exe`.

## SRT user: Using the libsrt installer

### Installing the SRT libraries

To install the SRT libraries, simply run the `libsrt-VERS.exe` installer which is
available in the [SRT release area](https://github.com/Haivision/srt/releases).

After installing the libsrt binaries, an environment variable named `LIBSRT` is
defined with the installation root (typically `C:\Program Files (x86)\libsrt`).

If there is a need for automation, in a CI/CD pipeline for instance, the download
of the latest `libsrt-VERS.exe` and its installation can be automated using the
sample PowerShell script `install-libsrt.ps1` which is available in this directory.
This script may be freely copied in the user's build environment.

When run without parameters (for instance from the Windows explorer), this
script downloads and installs the latest version of libsrt.

Optional parameters:

- `-Destination path` :
  Specify a local directory where the libsrt package will be downloaded.
  By default, use the `tmp` subdirectory from this script's directory.

- `-ForceDownload` :
  Force a download even if the package is already downloaded in the
  destination path. Note that the latest version is always checked.
  If a older package is already present but a newer one is available
  online, the newer one is always downloaded, even without this option.

- `-GitHubActions` :
  When used in a GitHub Actions workflow, make sure that the `LIBSRT`
  environment variable is propagated to subsequent jobs. In your GitHub
  workflow, in the initial setup phase, use 
  `script-dir\install-libsrt.ps1 -GitHubActions -NoPause`.

- `-NoInstall` :
  Do not install the package, only download it. By default, libsrt is installed.

- `-NoPause` :
  Do not wait for the user to press `<enter>` at end of execution. By default,
  execute a `pause` instruction at the end of execution, which is useful
  when the script was launched from Windows Explorer. Use that option when the
  script is invoked from another PowerShell script.

### Building Windows applications with libsrt

In the SRT installation root directory (specified in environment variable `LIBSRT`),
there is a Visual Studio property file named `libsrt.props`. Simply reference this
property file in your Visual Studio project to use libsrt.

You can also do that manually by editing the application project file (the XML
file named with a `.vcxproj` extension). Add the following line just before
the end of the file:

~~~
<Import Project="$(LIBSRT)\libsrt.props"/>
~~~

With this setup, just compile your application normally, either using the
Visual Studio IDE or the MSBuild command line tool.

## Files reference

This directory contains the following files:

| File name               | Usage
| ----------------------- | -----
| build-win-installer.ps1 | PowerShell script to build the libsrt installer.
| install-libsrt.ps1      | Sample PowerShell script to automatically install libsrt (for user's projects).
| install-openssl.ps1     | PowerShell script to install OpenSSL (prerequisite to build the installer).
| install-nsis.ps1        | PowerShell script to install NSIS (prerequisite to build the installer).
| libsrt.nsi              | NSIS installation script (used to build the installer).
| libsrt.props            | Visual Studio property files to use libsrt (embedded in the installer).
| README.md               | This text file.
