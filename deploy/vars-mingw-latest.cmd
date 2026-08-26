@ECHO OFF
REM Toolchain for modern Windows: MinGW 13.1, 64-bit.
REM Shipped with the Qt online installer as Tools\mingw1310_64.

SET _ROOT_QT=C:\DEV\Qt
SET _MINGW_VERSION=mingw1310_64

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja
SET _ROOT_MINGW=%_ROOT_QT%\Tools\%_MINGW_VERSION%\bin

SET PATH=%_ROOT_MINGW%;%_ROOT_CMAKE%;%_ROOT_NINJA%;%PATH%
