@ECHO OFF
REM Toolchain for the browser: Emscripten from emsdk.
REM Tested with 5.0.4. `emsdk_env.bat` puts emcc, node and its own Python
REM into PATH; CMake and Ninja still come from the Qt tools, as everywhere
REM else in deploy/.

SET _ROOT_EMSDK=C:\DEV\emsdk
SET _ROOT_QT=C:\DEV\Qt

CALL "%_ROOT_EMSDK%\emsdk_env.bat" >NUL

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%PATH%
