@echo off
setlocal

set PROJECT_DIR=%CD%
set BUILD_DIR=%PROJECT_DIR%\build
@REM set CONFIG=Debug
set RETROARCH_DIR=D:\dev\RetroArch-Win64
@REM set CORE_NAME=libretro_core_vulkan.dll
set CORE_NAME=liblibretro_vulkan.dll
@REM set CONTENT=script.zip
@REM set ZIP="C:\Program Files\7-Zip\7z.exe"
@REM set ROM=assets.zip

@REM compress file
@REM %ZIP% a %ROM% script.lua
@REM %ZIP% a %ROM% ./assets/*

@REM retroarch.exe -L libretro_core_glad_lua.dll script.zip
@REM %RETROARCH_DIR%/retroarch -L %BUILD_DIR%/%CONFIG%/%CORE_NAME% %ROM%
%RETROARCH_DIR%/retroarch -L %BUILD_DIR%/%CORE_NAME%

endlocal