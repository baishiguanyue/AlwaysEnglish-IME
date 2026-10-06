@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set BUILD_TYPE=Release
set BUILD_DIR=out\build\x64-Release

set "CMAKE="
where cmake >nul 2>&1 && (
    for /f "delims=" %%I in ('where cmake') do (
        set "CMAKE=%%I"
        goto :have_cmake
    )
)

if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    goto :have_cmake
)
if exist "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE=D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    goto :have_cmake
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find **\cmake.exe`) do (
        set "CMAKE=%%I"
        goto :have_cmake
    )
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
        if exist "%%I\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "CMAKE=%%I\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            goto :have_cmake
        )
    )
)

echo cmake.exe not found. Install Visual Studio with the C++ CMake tools.
exit /b 1

:have_cmake
echo Using cmake: %CMAKE%

"%CMAKE%" -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILD_DIR%" --config %BUILD_TYPE%
if errorlevel 1 exit /b 1

call installer\build.bat
if errorlevel 1 exit /b 1

echo.
echo Packaged: %~dp0out\installer\KeyboardMethod-Setup.exe
pause
exit /b 0
