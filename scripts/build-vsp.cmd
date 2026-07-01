@echo off
setlocal EnableExtensions

rem Builds vsp with MSVC stdlib on PATH (vcvars64). Use from any shell — no need
rem to start Cursor from "x64 Native Tools".
rem
rem   build-vsp.cmd
rem   build-vsp.cmd "F:\path\to\VSP_Build_BryAI"
rem   build-vsp.cmd "F:\path\to\VSP_Build_BryAI" Debug
rem   build-vsp.cmd "F:\path\to\VSP_Build_BryAI" RelWithDebInfo vsp

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "OPENVSP_ROOT=%%~fI"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere not found: "%VSWHERE%"
  exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"

if not defined VSINSTALL (
  echo ERROR: No Visual Studio install with VC Tools ^(x86/x64^) found.
  exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo ERROR: vcvars64.bat not found: "%VCVARS%"
  exit /b 1
)

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%OPENVSP_ROOT%\VSP_Build_BryAI"

set "CFG=%~2"
if "%CFG%"=="" set "CFG=RelWithDebInfo"

set "TGT=%~3"
if "%TGT%"=="" set "TGT=vsp"

call "%VCVARS%" || exit /b 1
cmake --build "%BUILD_DIR%" --config "%CFG%" --target "%TGT%" -j 8
exit /b %ERRORLEVEL%
