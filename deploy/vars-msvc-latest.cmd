@ECHO OFF
REM Toolchain for modern Windows: MSVC, x64.
REM Portable MSVC laid out by DSKCommander\.build\portable-msvc.py;
REM setup_x64.bat puts cl.exe and the SDK on PATH.

SET _ROOT_QT=C:\DEV\Qt
SET _ROOT_MSVC=C:\DEV\MSVC\msvc

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja

CALL "%_ROOT_MSVC%\setup_x64.bat"

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%PATH%
