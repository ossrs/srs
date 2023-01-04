rem Create empty directories for package bundle
@echo off

IF "%PLATFORM%"=="x86" (
    SET FOLDER_PLATFORM="32"
) ELSE IF "%PLATFORM%"=="x64" (
    SET FOLDER_PLATFORM="64"
) ELSE (
    echo "Platform %PLATFORM% is not supported"
    exit 1
)

md %APPVEYOR_BUILD_FOLDER%\package
md %APPVEYOR_BUILD_FOLDER%\package\include
md %APPVEYOR_BUILD_FOLDER%\package\include\win
md %APPVEYOR_BUILD_FOLDER%\package\bin        
md %APPVEYOR_BUILD_FOLDER%\package\lib
IF "%GATHER_SSL_INTO_PACKAGE%"=="True" (
    md %APPVEYOR_BUILD_FOLDER%\package\openssl-win%FOLDER_PLATFORM%
)

rem Gather SRT includes, binaries and libs
copy %APPVEYOR_BUILD_FOLDER%\version.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\srtcore\*.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\haicrypt\*.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\common\*.h %APPVEYOR_BUILD_FOLDER%\package\include\
copy %APPVEYOR_BUILD_FOLDER%\common\win\*.h %APPVEYOR_BUILD_FOLDER%\package\include\win\
copy %APPVEYOR_BUILD_FOLDER%\_build\%CONFIGURATION%\*.exe %APPVEYOR_BUILD_FOLDER%\package\bin\
copy %APPVEYOR_BUILD_FOLDER%\_build\%CONFIGURATION%\*.dll %APPVEYOR_BUILD_FOLDER%\package\bin\
copy %APPVEYOR_BUILD_FOLDER%\_build\%CONFIGURATION%\*.lib %APPVEYOR_BUILD_FOLDER%\package\lib\
copy %APPVEYOR_BUILD_FOLDER%\_build\%CONFIGURATION%\*.pdb %APPVEYOR_BUILD_FOLDER%\package\bin\

rem Gather 3rd party openssl elements
IF "%GATHER_SSL_INTO_PACKAGE%"=="True" (
    (robocopy c:\openssl-win%FOLDER_PLATFORM%\ %APPVEYOR_BUILD_FOLDER%\package\openssl-win%FOLDER_PLATFORM% /s /e /np) ^& IF %ERRORLEVEL% GTR 1 exit %ERRORLEVEL%
)
exit 0
