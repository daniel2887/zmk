@echo off
setlocal

:: Get the directory where the script is located (absolute path with trailing backslash)
set "SCRIPT_DIR=%~dp0"

:: Activate virtual environment
if exist "%SCRIPT_DIR%.venv\Scripts\activate.bat" (
    echo [INFO] Activating virtual environment...
    call "%SCRIPT_DIR%.venv\Scripts\activate.bat"
) else (
    echo [!] ERROR: Virtual environment not found at %SCRIPT_DIR%.venv
    echo [!] Please ensure the virtual environment is set up.
    exit /b 1
)

:: Resolve absolute paths to dependencies using SCRIPT_DIR
:: We convert backslashes to forward slashes for compatibility with Zephyr/West
for %%i in ("%SCRIPT_DIR%..\zmk-config") do set "CORNE_MODULE=%%~fi"
set "CORNE_MODULE=%CORNE_MODULE:\=/%"
set "CORNE_CONFIG=%CORNE_MODULE%/config"

:: Create output directory immediately so it's ready for the first-finisher
if not exist "%SCRIPT_DIR%output" mkdir "%SCRIPT_DIR%output"

:: Define the base build commands
set "LEFT_BUILD=west build --pristine=auto -b nice_nano//zmk -d build/left -- -DSHIELD="corne_left" -DZMK_CONFIG="%CORNE_CONFIG%""
set "RIGHT_BUILD=west build --pristine=auto -b nice_nano//zmk -d build/right -- -DSHIELD="corne_right" -DZMK_CONFIG="%CORNE_CONFIG%""

pushd "%SCRIPT_DIR%app"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=both"

echo ---------------------------------------
echo Launching Builds (%TARGET%)...
echo ---------------------------------------

if /i "%TARGET%"=="left" goto build_left_sync
if /i "%TARGET%"=="right" goto build_right_sync
if /i "%TARGET%"=="both" goto build_both

echo [!] Unknown target: %TARGET%
echo Usage: build_corne.bat [left^|right^|both]
popd
exit /b 1

:build_both
:: Launch Left: Build -> Copy -> Pause
start "Corne Left" cmd /c "%LEFT_BUILD% && (echo. & echo Copying Left firmware... & copy build\left\zephyr\zmk.uf2 ..\output\corne_left.uf2 /Y & echo. & echo DONE - Press any key to close window) || (echo. & echo [!] LEFT BUILD FAILED) & pause"

:: Launch Right: Build -> Copy -> Pause
start "Corne Right" cmd /c "%RIGHT_BUILD% && (echo. & echo Copying Right firmware... & copy build\right\zephyr\zmk.uf2 ..\output\corne_right.uf2 /Y & echo. & echo DONE - Press any key to close window) || (echo. & echo [!] RIGHT BUILD FAILED) & pause"

popd

echo.
echo Builds are running in separate windows.
echo Files will appear in 'output/' as soon as they are ready.
echo.
exit /b 0

:build_left_sync
echo Building Left...
%LEFT_BUILD%
if %errorlevel% neq 0 (
    echo.
    echo [!] LEFT BUILD FAILED
    popd
    exit /b 1
)
echo.
echo Copying Left firmware...
copy build\left\zephyr\zmk.uf2 ..\output\corne_left.uf2 /Y
echo.
echo DONE
popd
exit /b 0

:build_right_sync
echo Building Right...
%RIGHT_BUILD%
if %errorlevel% neq 0 (
    echo.
    echo [!] RIGHT BUILD FAILED
    popd
    exit /b 1
)
echo.
echo Copying Right firmware...
copy build\right\zephyr\zmk.uf2 ..\output\corne_right.uf2 /Y
echo.
echo DONE
popd
exit /b 0