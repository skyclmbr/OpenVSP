@echo off
setlocal EnableExtensions

rem Build vsp with VR enabled (VSP_VR=ON configure required).
rem   build-vr.cmd
rem   build-vr.cmd "F:\path\to\VSP_Build_VR"
rem   build-vr.cmd "F:\path\to\VSP_Build_VR" RelWithDebInfo

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "OPENVSP_ROOT=%%~fI"

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%OPENVSP_ROOT%\VSP_Build_VR"

set "CFG=%~2"
if "%CFG%"=="" set "CFG=RelWithDebInfo"

call "%OPENVSP_ROOT%\scripts\build-vsp.cmd" "%BUILD_DIR%" "%CFG%" vsp
exit /b %ERRORLEVEL%
