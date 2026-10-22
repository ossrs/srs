#!/bin/bash
# Check that the Windows test host has every tool the native Windows build and tests of SRS need,
# from macOS or Linux over SSH.
#
#   srs-windows-env-check.sh
#
# Opt in with SRS_TEST_WINDOWS_HOST, the SSH host of the Windows machine, set in your shell profile
# such as ~/.zshrc:
#   export SRS_TEST_WINDOWS_HOST=<srs-test-windows-host>
#
# The tools, checked where the build and tests find them:
#   - Visual Studio 2022 or its Build Tools with the C++ x64 tools, and after vcvars64: cl, and the
#     CMake and vcpkg that come with it.
#   - Git Bash, and on its PATH, as the runner sees them: GNU make, Go and FFmpeg.
#   - OBS Studio in its default folder.
#   - MSYS2 in C:\msys64, with make, nasm and pkgconf, to build FFmpeg with --toolchain=msvc.
#
# Prints a TOOL line with the version or a MISSING line with a hint per tool, then RESULT.
# Exit 0 when every tool is there, 1 when one is missing, 2 when skipped because the host is not set
# or cannot be reached, and 3 for bad arguments or when the check did not finish on the host.

if [[ $# -gt 0 ]]; then
  echo "Usage: $0"
  exit 3
fi

HOST=$SRS_TEST_WINDOWS_HOST
if [[ -z $HOST ]]; then
  echo "SKIP windows: SRS_TEST_WINDOWS_HOST is not set"
  exit 2
fi
if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$HOST" exit >/dev/null 2>&1; then
  echo "SKIP windows: cannot ssh to $HOST"
  exit 2
fi

mkdir -p /tmp/srs-windows-env-check
PS1=srs-windows-env-check-$$.ps1
REMOTE_TEMP=AppData/Local/Temp
cleanup() {
  ssh -o BatchMode=yes "$HOST" "powershell -NoProfile -Command Remove-Item -ErrorAction SilentlyContinue $REMOTE_TEMP/$PS1" >/dev/null 2>&1
  rm -f /tmp/srs-windows-env-check/srs-windows-env-check-$$.ps1 /tmp/srs-windows-env-check/srs-windows-env-check-$$.out
}
trap cleanup EXIT

cat > /tmp/srs-windows-env-check/$PS1 <<'EOF'
function Report([string]$Name, [string]$Version, [string]$Hint) {
    if ($Version) { "TOOL ${Name}: $($Version.Trim())" } else { "MISSING ${Name}: $Hint" }
}
# The first line a command prints, or nothing when it fails. The output is collected in full first:
# Select-Object -First stops the command early and loses its exit code.
function FirstLine([scriptblock]$Block) {
    try {
        $out = @(& $Block 2>$null)
        if ($LASTEXITCODE -eq 0 -and $out.Count -gt 0) { return "$($out[0])" }
    } catch {}
}
function FileVersion([string]$Path) {
    if ($Path -and (Test-Path $Path)) { (Get-Item $Path).VersionInfo.ProductVersion }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null; $vsVersion = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -version '[17.0,' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vsVersion = & $vswhere -latest -products * -version '[17.0,' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
}
Report 'vs' $vsVersion 'install Visual Studio 2022 Build Tools with the C++ x64 tools, CMake and vcpkg'
if ($vs) {
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
}
$cl = Get-Command cl -ErrorAction SilentlyContinue
Report 'cl' (FileVersion $cl.Source) 'needs the VS C++ x64 tools'
Report 'cmake' (FirstLine { cmake --version }) 'needs the VS C++ CMake tools component'
Report 'vcpkg' (FirstLine { vcpkg version }) 'needs the VS vcpkg component'

$bash = "$env:ProgramFiles\Git\bin\bash.exe"
$hasBash = Test-Path $bash
Report 'git-bash' $(if ($hasBash) { FirstLine { & $bash --version } }) 'install Git for Windows'
if ($hasBash) {
    Report 'make' (FirstLine { & $bash -c 'make --version' }) 'winget install ezwinports.make'
    Report 'go' (FirstLine { & $bash -c 'go version' }) 'winget install GoLang.Go'
    Report 'ffmpeg' (FirstLine { & $bash -c 'ffmpeg -version' }) 'winget install Gyan.FFmpeg'
} else {
    foreach ($t in 'make', 'go', 'ffmpeg') { Report $t $null 'needs Git Bash first' }
}

Report 'obs' (FileVersion "$env:ProgramFiles\obs-studio\bin\64bit\obs64.exe") 'winget install OBSProject.OBSStudio'

$msys = 'C:\msys64'
$hasMsys = Test-Path "$msys\usr\bin\bash.exe"
Report 'msys2' $(if ($hasMsys) { $msys }) 'winget install MSYS2.MSYS2'
function MsysTool([string]$Name, [string]$Exe) {
    $path = "$msys\usr\bin\$Exe.exe"
    $v = $null
    if (Test-Path $path) { $v = FirstLine { & $path --version } }
    Report $Name $v "C:\msys64\usr\bin\pacman -S --noconfirm $Exe"
}
MsysTool 'msys2-make' 'make'
MsysTool 'msys2-nasm' 'nasm'
MsysTool 'msys2-pkgconf' 'pkgconf'
'END'
EOF

if ! scp -q /tmp/srs-windows-env-check/$PS1 "$HOST:$REMOTE_TEMP/$PS1"; then
  echo "SETUP_FAIL: cannot copy the check to $HOST"
  exit 3
fi
ssh -o BatchMode=yes "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -File $REMOTE_TEMP/$PS1" | tr -d '\r' |
  tee /tmp/srs-windows-env-check/srs-windows-env-check-$$.out
if ! grep -q '^END$' /tmp/srs-windows-env-check/srs-windows-env-check-$$.out; then
  echo "SETUP_FAIL: the check did not finish on $HOST"
  exit 3
fi
MISSING=$(grep -c '^MISSING ' /tmp/srs-windows-env-check/srs-windows-env-check-$$.out)
if [[ $MISSING != 0 ]]; then
  echo "RESULT windows-env FAIL $MISSING missing"
  exit 1
fi
echo "RESULT windows-env PASS"
exit 0
