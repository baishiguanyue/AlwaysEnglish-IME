@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "ISCC="
for %%P in (
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
    "D:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    "D:\Program Files\Inno Setup 6\ISCC.exe"
) do (
    if exist "%%~fP" set "ISCC=%%~fP"
)

if not defined ISCC (
    echo ISCC.exe not found. Install Inno Setup 6.
    exit /b 1
)

set "DIST="
for %%P in (
    "..\out\build\x64-Release\bin\Release"
    "..\out\build\x64-Release\bin"
) do (
    if exist "%%~fP\AlwaysEnglishIME.dll" if exist "%%~fP\imeinst.exe" (
        set "DIST=%%~fP"
        goto :found
    )
)

echo AlwaysEnglishIME.dll / imeinst.exe not found. Build the project first.
exit /b 1

:found
echo Using: %DIST%
"%ISCC%" /DDistDir="%DIST%" AlwaysEnglishIME.iss
exit /b %ERRORLEVEL%
