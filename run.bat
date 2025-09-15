@echo off
setlocal

set PROJECT_DIR=%CD%
set BUILD_DIR=%PROJECT_DIR%\build
@REM set CONFIG=Debug
set RETROARCH_DIR=D:\dev\RetroArch-Win64
set CORE_NAME=liblibretro_vulkan.dll
%RETROARCH_DIR%/retroarch -L %BUILD_DIR%/%CORE_NAME%

endlocal