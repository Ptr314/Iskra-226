@ECHO OFF
REM Modern Windows, 64-bit, MSVC. CMAKE_MSVC_RUNTIME_LIBRARY pulls the
REM runtime into the .exe, so the zip needs no redistributable.

CALL "%~dp0vars-msvc-latest.cmd"
CD /D "%~dp0."

SET _PLATFORM=windows
SET _ARCHITECTURE=x86_64
SET _COMPILER=msvc
SET _BUILD_DIR=.\build\%_PLATFORM%_%_ARCHITECTURE%_%_COMPILER%

SET /P _VERSION=<..\VERSION
SET _RELEASE_NAME=iskra-%_VERSION%-%_PLATFORM%-%_ARCHITECTURE%-%_COMPILER%

cmake -S .. -B "%_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded || EXIT /B 1
cmake --build "%_BUILD_DIR%" || EXIT /B 1
ctest --test-dir "%_BUILD_DIR%" --output-on-failure || EXIT /B 1

CALL "%~dp0pack.cmd" "%_BUILD_DIR%" "%_RELEASE_NAME%" || EXIT /B 1
