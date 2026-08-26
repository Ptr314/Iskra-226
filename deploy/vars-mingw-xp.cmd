@ECHO OFF
REM Toolchain for Windows XP: MinGW 4.9.2, 32-bit.
REM Shipped with Qt 5.6 as Tools\mingw492_32. This is the toolchain the
REM C++11-only rule of this project exists for.

SET _ROOT_QT=C:\DEV\Qt
SET _MINGW_VERSION=mingw492_32

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja
SET _ROOT_MINGW=%_ROOT_QT%\Tools\%_MINGW_VERSION%\bin

SET PATH=%_ROOT_MINGW%;%_ROOT_CMAKE%;%_ROOT_NINJA%;%PATH%
