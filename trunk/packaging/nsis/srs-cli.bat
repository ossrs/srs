for /f "tokens=2*" %%i in ('REG QUERY "HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\App Paths\srs\ins_dir"') do set srs_home=%%j

echo %srs_home%

for %%I in ("%srs_home%") do set srs_disk=%%~dI

cd %srs_home%
@%srs_disk%

objs\srs.exe -c conf\console.conf
cmd