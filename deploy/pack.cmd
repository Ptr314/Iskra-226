@ECHO OFF
REM Collects a release directory and zips it.
REM   %1 - build directory holding the freshly built .exe files
REM   %2 - release name, e.g. iskra-0.1.0-windows-i386

SETLOCAL

SET _BUILD_DIR=%~1
SET _RELEASE_NAME=%~2
SET _RELEASE_DIR=.\release\%_RELEASE_NAME%

IF EXIST "%_RELEASE_DIR%" RMDIR /S /Q "%_RELEASE_DIR%"
MKDIR "%_RELEASE_DIR%"

COPY "%_BUILD_DIR%\iskra.exe" "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "%_BUILD_DIR%\iskra-nohead.exe" "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "..\README.md" "%_RELEASE_DIR%" >NUL || EXIT /B 1
COPY "..\LICENSE" "%_RELEASE_DIR%" >NUL || EXIT /B 1

IF EXIST ".\release\%_RELEASE_NAME%.zip" DEL ".\release\%_RELEASE_NAME%.zip"

SET _SEVENZIP=
IF EXIST "C:\Program Files\7-Zip\7z.exe"       SET _SEVENZIP=C:\Program Files\7-Zip\7z.exe
IF EXIST "C:\Program Files (x86)\7-Zip\7z.exe" SET _SEVENZIP=C:\Program Files (x86)\7-Zip\7z.exe

IF DEFINED _SEVENZIP (
    PUSHD "%_RELEASE_DIR%"
    "%_SEVENZIP%" a "..\%_RELEASE_NAME%.zip" * -mx9 >NUL || (POPD & EXIT /B 1)
    POPD
) ELSE (
    ECHO 7-Zip not found, falling back to PowerShell
    powershell -NoProfile -Command "Compress-Archive -Path '%_RELEASE_DIR%\*' -DestinationPath '.\release\%_RELEASE_NAME%.zip' -Force" || EXIT /B 1
)

ECHO.
ECHO   release\%_RELEASE_NAME%.zip
ECHO.
ENDLOCAL
