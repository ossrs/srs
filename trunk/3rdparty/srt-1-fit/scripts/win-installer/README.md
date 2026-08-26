# SRT Static Libraries Installer for Windows

This directory contains scripts to build a binary installer for
libsrt on Windows systems for Visual Studio applications using SRT.

## SRT developer: Building the libsrt installer

### Prerequisites

These initial steps need to be executed once only.

- Prerequisite 1: Install Visual Studio. The free Community Edition is recommended
  for open-source software. See https://visualstudio.microsoft.com/. Be sure to
  install the compilation tools for the all target architectures, including 64-bit Arm.
  In the Visual Studio Installer program, select "Modify" -> "Individual Components".
  In section "Compilers, build tools, and runtimes", select "MSVC v143 - VS2022 C++
  ARM64/ARM64EC build tools (latest)" (the exact version may vary).

- Prerequisite 2: Install OpenSSL for Windows, 64-bit Intel, 32-bit Intel, 64-bit Arm.
  This can be done automatically by running the PowerShell script `install-openssl.ps1`.
  If you are interested in more information about OpenSSL for Windows, scroll to the
  appendix at the end of this page.

- Prerequisite 3: Install NSIS, the NullSoft Installation Scripting system.
  This can be done automatically by running the PowerShell script `install-nsis.ps1`.

### Building the libsrt installer

To build the libsrt installer, simply run the PowerShell script `build-win-installer.ps1`.
Running it without parameters, for instance launching it from the Windows Explorer, is
sufficient to build the installer.

Optional parameters:

- `-Version name` :
  Use the specified string as version number for libsrt. By default, if the
  current commit has a tag, use that tag (initial "v" removed, for instance
  `1.4.3`). Otherwise, the default version is a detailed version number (most
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

## Appendix: Using OpenSSL on Windows

The SRT protocol uses encryption. Internally, `libsrt` can use multiple cryptographic
libraries but OpenSSL is the default choice on all platforms, including Windows.

OpenSSL is not part of the base installation of a Windows platform and should be
separately installed. OpenSSL is neither "Windows-friendly" and the OpenSSL team
does not provide Windows binaries.

The production of OpenSSL binaries for Windows is delegated to Shining Light
Productions (http://slproweb.com/). Binaries for all architectures are available
here: http://slproweb.com/products/Win32OpenSSL.html

### How to grab OpenSSL installers for Windows

This is automatically done by the PowerShell script `install-openssl.ps1` in
this directory. Let's see what's behind the scene.

First, the script determines which installers should be downloaded and where
to get them from.

Initially, the HTML for the [slproweb](http://slproweb.com/products/Win32OpenSSL.html)
page was built on server-side. Our script grabbed the URL content, parsed the HTML
and extracted the URL of the binaries for the latest OpenSSL packages from the
"href" of the links.

At some point in 2024, the site changed policy. The Web page was no longer
built on server-side, but on client-side. The downloaded HTML contains some
JavaScript which dynamically builds the URL of the binaries for the latest
OpenSSL binaries. The previous method now finds no valid package URL from
the downloaded page (a full browser is needed to execute the JavaScript).
The JavaScript downloads a JSON file which contains all references to
the OpenSSL binaries.

Therefore, the script `install-openssl.ps1` now uses the same method:
download that JSON file and parse it.

### Multi-architecture builds

The `libsrt` installer contains static libraries for all three architectures
which are supported by Windows: x86, x64, Arm64. The corresponding static
libraries for OpenSSL are also needed and included in the installer.

Because Visual Studio can build libraries and executables for all three
architectures, on any build system, we need to install the OpenSSL headers
and libraries for all three architectures, on the build system.

This is what the script `install-openssl.ps1` does. However, the way to
do this has changed over time.

As a starting point, there are three installers, one per architecture.
All installers are x86 executables which can be run on any Windows system
(Arm64 Windows emulates x86 and x64 when necessary). Additionally, the
three sets of files are installed side by side, without overlap.

- x86: `C:\Program Files (x86)\OpenSSL-Win32`
- x64: `C:\Program Files\OpenSSL-Win64`
- Arm64: `C:\Program Files\OpenSSL-Win64-ARM`

So, the script `install-openssl.ps1` installed them all and `libsrt` could
be built for all architecture.

However, it works only on Arm64 systems. On x86 and x64 system, the OpenSSL
installer for Arm64 fails. The installer executable can run but there is an
explicit check in the installation process to abort when running on Intel
systems.

Since most Windows build servers are Intel systems, this approach was no
longer appropriate.

At some point, the producers of the OpenSSL binaries acknowledged the problem
and they created an additional form of installer: the "universal" one. This
package can be installed on any system and the binaries and headers are installed
for all architectures, in one single location.

- Universal: `C:\Program Files (x86)\OpenSSL-WinUniversal`
