@ECHO OFF
REM Modern Windows, 64-bit, MinGW 13.1. Statically linked: the two
REM .exe files need nothing but the system DLLs.

CALL "%~dp0vars-mingw-latest.cmd"
CD /D "%~dp0."

SET _PLATFORM=windows
SET _ARCHITECTURE=x86_64
SET _COMPILER=mingw
SET _BUILD_DIR=.\build\%_PLATFORM%_%_ARCHITECTURE%_%_COMPILER%

SET /P _VERSION=<..\VERSION
SET _RELEASE_NAME=iskra-%_VERSION%-%_PLATFORM%-%_ARCHITECTURE%-%_COMPILER%

SET CC=%_ROOT_MINGW%\gcc.exe
SET CXX=%_ROOT_MINGW%\g++.exe

cmake -S .. -B "%_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || EXIT /B 1
cmake --build "%_BUILD_DIR%" || EXIT /B 1
ctest --test-dir "%_BUILD_DIR%" --output-on-failure || EXIT /B 1

CALL "%~dp0pack.cmd" "%_BUILD_DIR%" "%_RELEASE_NAME%" || EXIT /B 1
