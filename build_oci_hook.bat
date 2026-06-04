@echo off
REM SQL Audit Proxy Build Script
REM Builds SqlProxy.exe + OciHook.dll (pure C++, zero external deps)
REM Requires: Visual Studio 2022 with C++ desktop development workload
REM
REM Usage:
REM   build_oci_hook.bat              -> x64 Release
REM   build_oci_hook.bat debug        -> x64 Debug
REM   build_oci_hook.bat release win32 -> Win32 Release
REM   build_oci_hook.bat debug win32   -> Win32 Debug

echo === SQL Audit Build ===

set CONFIG=Release
if "%1"=="debug" set CONFIG=Debug
if "%2"=="debug"  set CONFIG=Debug

set PLATFORM=x64
if "%1"=="win32" set PLATFORM=Win32
if "%2"=="win32" set PLATFORM=Win32

set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist %MSBUILD% set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
if not exist %MSBUILD% set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"

if not exist %MSBUILD% (
    echo ERROR: MSBuild not found. Please install Visual Studio 2022.
    exit /b 1
)

echo Target: %PLATFORM% %CONFIG%

echo Building SqlProxy.exe...
%MSBUILD% SqlProxy\SqlProxy.vcxproj /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /v:minimal
if %ERRORLEVEL% NEQ 0 (echo ERROR: SqlProxy build failed! & exit /b 1)

echo Building OciHook.dll...
%MSBUILD% OciHook\OciHook.vcxproj /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /v:minimal
if %ERRORLEVEL% NEQ 0 (echo ERROR: OciHook build failed! & exit /b 1)

echo.
echo === Build Complete ===
echo Output: %PLATFORM%\%CONFIG%\
echo.
echo Deploy:
echo   xcopy %PLATFORM%\%CONFIG%\SqlProxy.exe C:\Tools\SqlProxy\
echo   xcopy %PLATFORM%\%CONFIG%\OciHook.dll  C:\Tools\SqlProxy\
echo   xcopy SqlProxy\config.json             C:\Tools\SqlProxy\
echo.
echo   Then: C:\Tools\SqlProxy\SqlProxy.exe [--service]
