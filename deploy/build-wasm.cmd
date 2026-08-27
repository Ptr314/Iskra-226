@ECHO OFF
REM Browser, canvas. Builds, runs the tests under node and packs a directory
REM ready to be dropped into a web root. Same output as build-wasm.sh.

CALL "%~dp0vars-emsdk.cmd"
CD /D "%~dp0."

SET _PLATFORM=web
SET _BUILD_DIR=.\build\%_PLATFORM%

SET /P _VERSION=<..\VERSION
SET _RELEASE_NAME=iskra-%_VERSION%-%_PLATFORM%
SET _RELEASE_DIR=.\release\%_RELEASE_NAME%

CALL emcmake cmake -S .. -B "%_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || EXIT /B 1
cmake --build "%_BUILD_DIR%" || EXIT /B 1
ctest --test-dir "%_BUILD_DIR%" --output-on-failure || EXIT /B 1

IF EXIST "%_RELEASE_DIR%" RMDIR /S /Q "%_RELEASE_DIR%"
MKDIR "%_RELEASE_DIR%\programs"

COPY "..\src\host_wasm\web\index.html"  "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "..\src\host_wasm\web\iskra.css"   "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "..\src\host_wasm\web\app.js"      "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "..\src\host_wasm\web\menu.js"     "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "..\src\host_wasm\web\keyboard.js" "%_RELEASE_DIR%" >NUL || EXIT /B 1

COPY "%_BUILD_DIR%\iskra.js"   "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "%_BUILD_DIR%\iskra.wasm" "%_RELEASE_DIR%" >NUL || EXIT /B 1

COPY ".\_bundle\bundle.json" "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY ".\_bundle\*.dsk" "%_RELEASE_DIR%" >NUL
COPY ".\_bundle\programs\*.bas" "%_RELEASE_DIR%\programs" >NUL

COPY ".\web\README.md"          "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY ".\web\nginx.conf.example" "%_RELEASE_DIR%" >NUL || EXIT /B 1

IF EXIST ".\release\%_RELEASE_NAME%.zip" DEL ".\release\%_RELEASE_NAME%.zip"

SET _SEVENZIP=
IF EXIST "C:\Program Files\7-Zip\7z.exe"       SET _SEVENZIP=C:\Program Files\7-Zip\7z.exe
IF EXIST "C:\Program Files (x86)\7-Zip\7z.exe" SET _SEVENZIP=C:\Program Files (x86)\7-Zip\7z.exe

IF DEFINED _SEVENZIP (
    PUSHD "%_RELEASE_DIR%"
    "%_SEVENZIP%" a "..\%_RELEASE_NAME%.zip" * -mx9 >NUL || (POPD & EXIT /B 1)
    POPD
) ELSE (
    powershell -NoProfile -Command "Compress-Archive -Path '%_RELEASE_DIR%\*' -DestinationPath '.\release\%_RELEASE_NAME%.zip' -Force" || EXIT /B 1
)

ECHO.
ECHO   release\%_RELEASE_NAME%.zip
ECHO   release\%_RELEASE_NAME%\  - can be dropped into a web root as is
ECHO.
