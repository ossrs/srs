:: Installs from srtp windows build directory to directory specified on
:: command line


@if "%1"=="" (
	echo "Usage: %~nx0 destdir"
	exit /b 1
) else (
	set destdir=%1
)

@if not exist %destdir% (
   echo %destdir% not found
   exit /b 1
)

@for %%d in (include\srtp.h crypto\include\cipher.h Debug\srtp2.lib Release\srtp2.lib x64\Debug\srtp2.lib x64\Release\srtp2.lib) do (
	if not exist "%%d" (
	   echo "%%d not found: are you in the right directory?"
	   exit /b 1
	)
)

mkdir %destdir%\include
mkdir %destdir%\include\srtp2
mkdir %destdir%\lib
mkdir %destdir%\lib\x64

@for %%d in (include\srtp.h include\ekt.h crypto\include\cipher.h crypto\include\auth.h crypto\include\crypto_types.h) do (
	 copy %%d %destdir%\include\srtp2
)
copy Release\srtp2.lib %destdir%\lib\srtp2.lib
copy Debug\srtp2.lib %destdir%\lib\srtp2d.lib
copy x64\Release\srtp2.lib %destdir%\lib\x64\srtp2.lib
copy x64\Debug\srtp2.lib %destdir%\lib\x64\srtp2d.lib
